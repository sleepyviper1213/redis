#!/usr/bin/env bash
# Differential test: start redis_server and a real Redis, and run
# scripts/diff_test.py against both: the same deterministic command stream,
# failing on the first reply that differs.
#
#   scripts/diff_server.sh [configure-preset] [configuration] [port] [reference-port]
#
# The reference is $REF_SERVER (default: redis-server on PATH). Options for the
# test go in DIFF_ARGS, e.g. to compare error replies by code only and list
# every divergence:
#   DIFF_ARGS="--loose-errors --keep-going" scripts/diff_server.sh
#
# A failing run prints its seed; replay it with DIFF_ARGS="--seed <n>".
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-Release}"
PORT="${3:-6385}"
REF_PORT="${4:-6386}"
REF_SERVER="${REF_SERVER:-redis-server}"

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
PYTHON="${PYTHON:-python3}"
WORKDIR="$(mktemp -d)"
mkdir -p "$WORKDIR/ref"

# No persistence for the reference: it starts empty and writes nothing.
"$REF_SERVER" --port "$REF_PORT" --bind 127.0.0.1 --save '' --appendonly no \
    --dir "$WORKDIR/ref" >"$WORKDIR/ref/redis.log" 2>&1 &
REF_PID=$!
# shellcheck disable=SC2086 # SERVER_ARGS is a list of words
start_server "$SERVER" "$PORT" "$WORKDIR" ${SERVER_ARGS:-}
trap 'kill "$SERVER_PID" "$REF_PID" 2>/dev/null || true' EXIT

for _ in $(seq 100); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$REF_PORT") 2>/dev/null; then break; fi
    if ! kill -0 "$REF_PID" 2>/dev/null; then
        echo "error: the reference server exited during startup:" >&2
        cat "$WORKDIR/ref/redis.log" >&2
        exit 1
    fi
    sleep 0.1
done

echo "Comparing $SERVER (port $PORT) with $("$REF_SERVER" --version) (port $REF_PORT)"
# shellcheck disable=SC2086 # DIFF_ARGS is a list of words
if ! "$PYTHON" scripts/diff_test.py --port "$PORT" --reference-port "$REF_PORT" ${DIFF_ARGS:-}; then
    echo "Differential test failed. Server log: $WORKDIR/redis_server.log" >&2
    exit 1
fi
stop_server "$WORKDIR"
