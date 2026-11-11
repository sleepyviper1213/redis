#!/usr/bin/env bash
# Profile redis_server with Linux `perf` while redis-benchmark loads it, for
# the sender sessions (v2) and the coroutine sessions (v1) one after the
# other, each against a fresh server. The Linux counterpart of
# profile_server.sh (macOS `sample`).
#
#   scripts/profile_server_linux.sh [configure-preset] [configuration] [scenario]
#
# Scenarios (as in bench_server.sh): clients (50 clients, no pipelining),
# pipeline (-P 16) and bigget (GET over KEYSPACE random keys, -P 16; the
# default). Profile a build with symbols (RelWithDebInfo), or the frames are
# unnamed.
#
# Knobs: PORT (default 6381), DURATION (seconds sampled, default 10), WARMUP
# (seconds of load before sampling, default 3), TOP (lines of the report,
# default 40), KEYSPACE (default 2000000) and VERSIONS (default "v1 v2").
#
# Needs `perf` and permission to use it: a privileged container with
# `sysctl kernel.perf_event_paranoid=-1`, or root on a VM. The report is the
# flat "where was the CPU" list. With CALLGRAPH=1 it also has an inclusive-time
# list (a function and everything it called), which needs a build with frame
# pointers: cmake --preset ... -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-RelWithDebInfo}"
SCENARIO="${3:-bigget}"
PORT="${PORT:-6381}"
DURATION="${DURATION:-10}"
WARMUP="${WARMUP:-3}"
TOP="${TOP:-40}"
INCLUSIVE_TOP="${INCLUSIVE_TOP:-120}"
KEYSPACE="${KEYSPACE:-2000000}"
VERSIONS="${VERSIONS:-v1 v2}"

case "$SCENARIO" in
    clients | pipeline | bigget) ;;
    *) echo "error: scenario must be clients, pipeline or bigget" >&2; exit 1 ;;
esac
command -v perf >/dev/null ||
    { echo "error: perf not found in PATH" >&2; exit 1; }
command -v redis-benchmark >/dev/null ||
    { echo "error: redis-benchmark not found in PATH" >&2; exit 1; }

# With CALLGRAPH set, sample call stacks too (frame pointers: build with
# CXXFLAGS=-fno-omit-frame-pointer), for the inclusive-time list.
CALLGRAPH_ARGS=()
if [[ -n "${CALLGRAPH:-}" ]]; then CALLGRAPH_ARGS=(--call-graph fp); fi

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
OUT="$(mktemp -d)"
SERVER_PID=""
BENCH_PID=""

cleanup() {
    for pid in "$BENCH_PID" "$SERVER_PID"; do
        if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; fi
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT

wait_for_port() {
    for _ in $(seq 1 50); do
        redis-benchmark -p "$PORT" -t ping -n 1 -q >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    echo "error: nothing is listening on port $PORT" >&2
    return 1
}

# The commands the server has handled so far (INFO's total_commands_processed).
commands_processed() {
    redis-cli -p "$PORT" info | tr -d '\r' |
        awk -F: '$1 == "total_commands_processed" { print $2 }'
}

# report_counters <perf stat -x, file> <commands handled during the window>
report_counters() {
    awk -F, -v n="$2" -v secs="$DURATION" '
        $3 ~ /context-switches/ { cs = $1 }
        $3 ~ /cpu-migrations/   { mg = $1 }
        $3 ~ /task-clock/       { tc = $1 }
        END {
            printf "   %d commands in %ds (%.0f/s)\n", n, secs, n / secs
            if (n > 0) {
                printf "   context-switches %d (%.2f per 1000 commands)\n", cs, cs * 1000 / n
                printf "   cpu-migrations   %d (%.2f per 1000 commands)\n", mg, mg * 1000 / n
                printf "   task-clock       %.0f ms (%.2f us per command)\n", tc, tc * 1000 / n
            }
        }' "$1"
}

# profile <version> <server args...>
profile() {
    local version="$1"
    shift
    local data="$OUT/$version.perf.data"

    # Logging is capped at error level: it would skew the profile.
    "$SERVER" --port "$PORT" --log-level error --log-file "$OUT/$version.log" \
        --dump "$OUT/$version.snap" "$@" >/dev/null 2>&1 &
    SERVER_PID=$!
    wait_for_port

    local load=(-t set -q -c 50)
    case "$SCENARIO" in
        clients) ;;
        pipeline) load+=(-P 16) ;;
        bigget)
            # Fill a keyspace too big for the CPU caches (random keys), then
            # read it back, as bench_server.sh does.
            redis-benchmark -p "$PORT" -t set -q -n "$((KEYSPACE * 2))" \
                -r "$KEYSPACE" -P 16 >/dev/null
            load=(-t get -q -r "$KEYSPACE" -P 16)
            ;;
    esac

    # Far more requests than DURATION needs: the load is stopped once the
    # sampling is done, so every sample is taken under load.
    redis-benchmark -p "$PORT" "${load[@]}" -n 1000000000 >/dev/null 2>&1 &
    BENCH_PID=$!
    sleep "$WARMUP"
    if ! kill -0 "$BENCH_PID" 2>/dev/null; then
        echo "error: redis-benchmark stopped before sampling began" >&2
        return 1
    fi

    echo "== $version, $SCENARIO: sampling pid $SERVER_PID for ${DURATION}s =="
    # Count over the same window as the samples, so the counters can be divided
    # by the commands the server handled: per-request figures, comparable
    # between versions even when the throughput differs.
    local before after stat_pid
    before="$(commands_processed)"
    perf stat -x, -e context-switches,cpu-migrations,task-clock \
        -p "$SERVER_PID" -o "$OUT/$version.stat" -- sleep "$DURATION" \
        >/dev/null 2>&1 &
    stat_pid=$!
    perf record -F 999 ${CALLGRAPH_ARGS[@]+"${CALLGRAPH_ARGS[@]}"} \
        -p "$SERVER_PID" -o "$data" -- sleep "$DURATION" \
        >/dev/null 2>"$OUT/$version.perf.err" ||
        { cat "$OUT/$version.perf.err" >&2; return 1; }
    wait "$stat_pid" || true
    after="$(commands_processed)"
    report_counters "$OUT/$version.stat" "$((after - before))"

    kill "$BENCH_PID" 2>/dev/null || true
    wait "$BENCH_PID" 2>/dev/null || true
    BENCH_PID=""
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""

    # One awk does the limiting: piping into head would kill perf with SIGPIPE,
    # which pipefail turns into a failure of the whole script.
    # Symbol names of stdexec chains run to thousands of characters: keep the
    # first 240 so each line stays readable (the numbers come first).
    echo "-- self time: where the CPU was --"
    perf report -i "$data" --stdio --no-children --sort symbol \
        --percent-limit 0.5 2>/dev/null |
        awk -v top="$TOP" '!/^#/ && NF && count++ < top { print substr($0, 1, 240) }'
    if [[ -n "${CALLGRAPH:-}" ]]; then
        # Needs frame pointers (-fno-omit-frame-pointer): each function's time
        # includes everything it called, so the cost of a whole stage of the
        # session shows up on one line. Columns: inclusive %, self %.
        echo
        # The top of this list is the event loop and the kernel's send path;
        # the session's own frames are lower, so list them on their own too.
        echo "-- inclusive time: a function and everything it called --"
        perf report -i "$data" --stdio --children --sort symbol -g none \
            --percent-limit 1.5 2>/dev/null |
            awk -v top="$INCLUSIVE_TOP" '!/^#/ && NF && count++ < top { print substr($0, 1, 240) }'
        local frames
        for frames in Session stdexec; do
            echo
            echo "-- inclusive time, frames matching '$frames' --"
            perf report -i "$data" --stdio --children --sort symbol -g none \
                --symbol-filter="$frames" --percent-limit 0.3 2>/dev/null |
                awk -v top="$TOP" '!/^#/ && NF && count++ < top { print substr($0, 1, 240) }'
        done
    fi
    echo
}

for version in $VERSIONS; do
    case "$version" in
        v1) profile v1 ;;
        v2) profile v2 --sender-sessions ;;
        *) echo "error: unknown version '$version'" >&2; exit 1 ;;
    esac
done
