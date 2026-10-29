#!/usr/bin/env bash
# Run the verifying stress test against a freshly started server.
#
#   scripts/stress_server.sh [configure-preset] [configuration] [port]
#
# Server options go in SERVER_ARGS, stress options in STRESS_ARGS, e.g. to
# stress the sender-based sessions with several I/O threads:
#   SERVER_ARGS="--sender-sessions --io-threads 4" \
#   STRESS_ARGS="--clients 64 --ops 50000 --pipeline 64" \
#       scripts/stress_server.sh
#
# A failing run prints its seed; replay it with STRESS_ARGS="--seed <n>".
# Combine with a ThreadSanitizer preset to catch data races the replies alone
# would not show.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-Debug}"
PORT="${3:-6381}"

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
STRESS="$(locate_binary "$PRESET" "$CONFIG" redis_stress_test)"
WORKDIR="$(mktemp -d)"
LOG="$WORKDIR/redis_server.log"

"$SERVER" --port "$PORT" --log-level info --log-file "$LOG" \
    --dump "$WORKDIR/dump.snap" ${SERVER_ARGS:-} >/dev/null 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

# Wait up to 10s for the port to accept connections.
for _ in $(seq 100); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then break; fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "error: server exited during startup:" >&2
        cat "$LOG" >&2
        exit 1
    fi
    sleep 0.1
done

echo "Running $STRESS against $SERVER on port $PORT"
if ! "$STRESS" --port "$PORT" ${STRESS_ARGS:-}; then
    echo "Stress test failed. Server log: $LOG" >&2
    exit 1
fi

# The server must still be serving after the stress.
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "error: the server died during the stress test. Log: $LOG" >&2
    exit 1
fi
