#!/usr/bin/env bash
# Shared helpers for the scripts in this directory. Source, don't execute.

# locate_binary <configure-preset> <configuration> <target>
#
# Multi-config generators put binaries under build/<preset>/<subdir>/<Config>/.
locate_binary() {
    local preset="$1" config="$2" target="$3" candidate
    for candidate in \
        "build/$preset/src/app/$config/$target" \
        "build/$preset/test/$config/$target"; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    echo "error: $target not found for preset '$preset' ($config)." >&2
    # tr rather than ${config,,}: macOS ships bash 3.2.
    echo "       Build it first: cmake --build --preset $preset-$(echo "$config" | tr '[:upper:]' '[:lower:]')" >&2
    return 1
}

# start_server <binary> <port> <workdir> [server args...]
#
# Start the server in the background and wait up to 10s for its port. Its log
# goes to <workdir>/redis_server.log, its stderr (where sanitizers report) to
# <workdir>/server.stderr. Sets SERVER_PID.
start_server() {
    local binary="$1" port="$2" workdir="$3"
    shift 3
    "$binary" --port "$port" --log-level info --log-file "$workdir/redis_server.log" \
        --dump "$workdir/dump.snap" "$@" >/dev/null 2>"$workdir/server.stderr" &
    SERVER_PID=$!
    for _ in $(seq 100); do
        if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then return 0; fi
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "error: server exited during startup:" >&2
            cat "$workdir/redis_server.log" "$workdir/server.stderr" >&2
            return 1
        fi
        sleep 0.1
    done
    echo "error: server did not accept connections within 10s" >&2
    return 1
}

# stop_server <workdir>
#
# Stop the server started by start_server with SIGTERM and check that it shut
# down cleanly: exit status 0 and nothing from a sanitizer on stderr. A
# ThreadSanitizer report does not stop the server, so this is where a data race
# found while serving fails the run. Native Windows processes cannot be sent
# SIGTERM from bash, so there the server is only killed.
stop_server() {
    local workdir="$1" status=0
    case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        kill "$SERVER_PID" 2>/dev/null || true
        return 0
        ;;
    esac
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "error: the server died before it was stopped." >&2
        status=1
    fi
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" || status=$?
    if [ "$status" -ne 0 ]; then
        echo "error: the server did not shut down cleanly (status $status)." >&2
    fi
    if grep -q -E "WARNING: ThreadSanitizer|ERROR: (Address|Leak|ThreadSanitizer|UndefinedBehavior)Sanitizer|runtime error:" \
        "$workdir/server.stderr"; then
        echo "error: a sanitizer reported a problem in the server." >&2
        status=1
    fi
    if [ "$status" -ne 0 ]; then
        echo "---- server stderr ----" >&2
        cat "$workdir/server.stderr" >&2
        echo "---- end of server log ----" >&2
        tail -n 50 "$workdir/redis_server.log" >&2
        return 1
    fi
}
