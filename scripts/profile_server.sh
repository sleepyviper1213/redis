#!/usr/bin/env bash
# Profile redis_server with macOS `sample` while redis-benchmark loads it, one
# scenario at a time, each against a fresh server. The scenarios are the ones of
# bench_server.sh:
#
#   clients   50 parallel clients, no pipelining
#   pipeline  50 clients, 16 commands per batch
#
#   scripts/profile_server.sh [configure-preset] [configuration] [clients|pipeline|all]
#
# Profile a build with symbols (RelWithDebInfo), or the frames are unnamed.
# Knobs: PORT (default 6381), DURATION (seconds sampled, default 10), WARMUP
# (seconds of load before sampling, default 2), TOP (lines of the summary
# printed, default 25), and SERVER_ARGS, e.g. SERVER_ARGS="--io-threads 4".
#
# The full call trees are left in the printed directory; open them for the
# per-thread breakdown. The summary printed here is the "top of stack" list:
# where the CPU was when each sample was taken.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-macos-arm64-llvm}"
CONFIG="${2:-RelWithDebInfo}"
WHICH="${3:-all}"
PORT="${PORT:-6381}"
DURATION="${DURATION:-10}"
WARMUP="${WARMUP:-2}"
TOP="${TOP:-25}"

case "$WHICH" in
    clients | pipeline | all) ;;
    *) echo "error: scenario must be clients, pipeline or all" >&2; exit 1 ;;
esac
command -v sample >/dev/null ||
    { echo "error: sample not found (it ships with macOS)" >&2; exit 1; }
command -v redis-benchmark >/dev/null ||
    { echo "error: redis-benchmark not found in PATH" >&2; exit 1; }

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

# profile <name> <redis-benchmark args...>
profile() {
    local name="$1"
    shift
    local file="$OUT/$name.sample.txt"

    # Logging is capped at error level: it would skew the profile.
    "$SERVER" --port "$PORT" --log-level error --log-file "$OUT/$name.log" \
        --dump "$OUT/$name.snap" ${SERVER_ARGS:-} >/dev/null 2>&1 &
    SERVER_PID=$!
    wait_for_port

    # Far more requests than DURATION needs: the load is stopped once the
    # sampling is done, so every sample is taken under load.
    redis-benchmark -p "$PORT" -t set -q -n 1000000000 "$@" >/dev/null 2>&1 &
    BENCH_PID=$!
    sleep "$WARMUP"
    if ! kill -0 "$BENCH_PID" 2>/dev/null; then
        echo "error: redis-benchmark stopped before sampling began" >&2
        return 1
    fi

    echo "== $name: sampling pid $SERVER_PID for ${DURATION}s =="
    sample "$SERVER_PID" "$DURATION" 1 -file "$file" >/dev/null

    kill "$BENCH_PID" 2>/dev/null || true
    wait "$BENCH_PID" 2>/dev/null || true
    BENCH_PID=""
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""

    # One awk does the limiting: piping into head would kill it with SIGPIPE,
    # which pipefail turns into a failure of the whole script.
    awk -v top="$TOP" '/^Sort by top of stack/ { found = 1 }
                       found && count++ <= top' "$file"
    echo "   (full call tree: $file)"
    echo
}

if [[ "$WHICH" == clients || "$WHICH" == all ]]; then
    profile clients
fi
if [[ "$WHICH" == pipeline || "$WHICH" == all ]]; then
    profile pipeline -P 16
fi
echo "profiles kept in $OUT"
