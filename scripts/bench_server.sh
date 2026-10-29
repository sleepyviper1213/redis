#!/usr/bin/env bash
# Benchmark SET throughput of redis_server (and optionally a reference
# redis-server) with redis-benchmark, over three scenarios:
#
#   clients  50 parallel clients, no pipelining  (the default redis-benchmark run)
#   latency  1 client, one round trip at a time  (per-request overhead)
#   pipeline 50 clients, 16 commands per batch   (command execution cost)
#
#   scripts/bench_server.sh [configure-preset] [configuration] [port]
#
# Server options go in SERVER_ARGS, e.g. the sender-based sessions:
#   SERVER_ARGS="--sender-sessions --io-threads 4" scripts/bench_server.sh
#
# Other knobs: RUNS (repeats per scenario, default 3), REQUESTS (default
# 200000), and REF_PORT to also benchmark an already running reference server,
# e.g. Homebrew's: REF_PORT=6379.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-Release}"
PORT="${3:-6381}"
RUNS="${RUNS:-3}"
REQUESTS="${REQUESTS:-200000}"

command -v redis-benchmark >/dev/null ||
    { echo "error: redis-benchmark not found in PATH" >&2; exit 1; }

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
WORKDIR="$(mktemp -d)"

# Cap the server at error level: logging would skew the numbers.
"$SERVER" --port "$PORT" --log-level error --log-file "$WORKDIR/server.log" \
    --dump "$WORKDIR/dump.snap" ${SERVER_ARGS:-} >/dev/null 2>&1 &
SERVER_PID=$!
cleanup() {
    local status=$?
    if [[ $status -ne 0 ]]; then
        # A reset connection usually means the server died: say so, and show
        # its log before the work directory is removed.
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "note: the server is still running (it closed the connection)" >&2
        else
            echo "error: the server exited during the run" >&2
        fi
        echo "--- last server log lines ---" >&2
        tail -n 20 "$WORKDIR/server.log" >&2 2>/dev/null || true
    fi
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

wait_for_port() {
    local port="$1"
    for _ in $(seq 1 50); do
        redis-benchmark -p "$port" -t ping -n 1 -q >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    echo "error: nothing is listening on port $port" >&2
    return 1
}

# run_scenario <port> <label> <redis-benchmark args...>: prints requests/second
# for each run, then the best one (the least disturbed by other processes).
run_scenario() {
    local port="$1" label="$2"
    shift 2
    local best=0 rps results=()
    for _ in $(seq 1 "$RUNS"); do
        rps="$(redis-benchmark -p "$port" -t set -q "$@" |
               tr '\r' '\n' | awk '/requests per second/ { v = $2 } END { print v }')"
        results+=("$rps")
        best="$(awk -v a="$best" -v b="$rps" 'BEGIN { print (b > a) ? b : a }')"
    done
    printf '  %-9s %s   best %s rps\n' "$label" "${results[*]}" "$best"
}

bench() {
    local name="$1" port="$2"
    echo "== $name (port $port, $REQUESTS requests, $RUNS runs) =="
    run_scenario "$port" clients  -n "$REQUESTS"
    run_scenario "$port" latency  -n "$((REQUESTS / 2))" -c 1
    run_scenario "$port" pipeline -n "$((REQUESTS * 2))" -P 16
}

wait_for_port "$PORT"
bench "redis_server ${SERVER_ARGS:-}" "$PORT"

if [[ -n "${REF_PORT:-}" ]]; then
    wait_for_port "$REF_PORT"
    bench "reference server" "$REF_PORT"
fi
