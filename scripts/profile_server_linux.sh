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
# default 40), KEYSPACE (default 2000000), VERSIONS (default "v1 v2") and
# ANNOTATE (a substring of the symbol whose hot instructions are listed, default
# "operator=="; empty for none). Compare a KEYSPACE that fits the CPU caches
# (10000) with one that does not to tell cache misses from compute: a symbol
# whose share collapses on the small one was waiting on memory.
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
ANNOTATE="${ANNOTATE-operator==}"

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
# The hardware counters (cycles, instructions, cache misses) are missing on VMs
# that do not expose a PMU; perf then writes "<not supported>", and they are
# left out.
report_counters() {
    awk -F, -v n="$2" -v secs="$DURATION" '
        function num(v) { return v ~ /^[0-9.]+$/ }
        $3 ~ /context-switches/ { cs = $1 }
        $3 ~ /cpu-migrations/   { mg = $1 }
        $3 ~ /task-clock/       { tc = $1 }
        $3 ~ /^cycles/          { if (num($1)) cy = $1 }
        $3 ~ /^instructions/    { if (num($1)) ins = $1 }
        $3 ~ /^cache-misses/    { if (num($1)) cm = $1 }
        $3 ~ /^LLC-load-misses/ { if (num($1)) llc = $1 }
        END {
            printf "   %d commands in %ds (%.0f/s)\n", n, secs, n / secs
            if (n > 0) {
                printf "   context-switches %d (%.2f per 1000 commands)\n", cs, cs * 1000 / n
                printf "   cpu-migrations   %d (%.2f per 1000 commands)\n", mg, mg * 1000 / n
                printf "   task-clock       %.0f ms (%.2f us per command)\n", tc, tc * 1000 / n
                if (cy != "")  printf "   cycles           %.0f per command\n", cy / n
                if (ins != "") printf "   instructions     %.0f per command\n", ins / n
                if (cy != "" && ins != "") printf "   IPC              %.2f\n", ins / cy
                if (cm != "")  printf "   cache-misses     %.2f per command\n", cm / n
                if (llc != "") printf "   LLC-load-misses  %.2f per command\n", llc / n
            }
        }' "$1"
}

# annotate <perf.data>: the instructions of the hottest symbol matching
# ANNOTATE, with the share of that symbol's samples on each, to see where in it
# the time goes. A load that misses the cache shows as a hot instruction just
# after it (or on it). Lines under 1% are dropped.
annotate() {
    local symbol
    symbol="$(perf report -i "$1" --stdio --no-children --sort symbol -g none 2>/dev/null |
        awk -v pat="$ANNOTATE" '!done && !/^#/ && index($0, pat) { sub(/^[^[]*\[[.k]\] /, ""); print; done = 1 }')"
    if [[ -z "$symbol" ]]; then
        echo "   no symbol matching '$ANNOTATE'"
        return 0
    fi
    echo "-- annotate: ${symbol:0:200} --"
    perf annotate -i "$1" --stdio -s "$symbol" 2>/dev/null |
        awk '$1 + 0 >= 1.0 && $2 == ":" { print substr($0, 1, 200) }' || true
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
        -e cycles,instructions,cache-misses,LLC-load-misses \
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
    # -g none: with CALLGRAPH the samples carry stacks, and perf would print a
    # call chain under every symbol, using up TOP lines on two entries.
    perf report -i "$data" --stdio --no-children --sort symbol -g none \
        --percent-limit 0.5 2>/dev/null |
        awk -v top="$TOP" '!/^#/ && NF && count++ < top { print substr($0, 1, 240) }'
    if [[ -n "$ANNOTATE" ]]; then
        echo
        annotate "$data"
    fi
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
