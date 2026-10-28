#!/usr/bin/env bash
# Run the integration suite against a freshly started server.
#
#   scripts/test_server.sh [configure-preset] [configuration] [port]
#
# Unit tests need no server and run through CTest instead:
#   ctest --preset linux-gcc-debug
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-Debug}"
PORT="${3:-6380}"

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
TESTS="$(locate_binary "$PRESET" "$CONFIG" redis_integration_test)"
WORKDIR="$(mktemp -d)"
LOG="$WORKDIR/redis_server.log"

"$SERVER" --port "$PORT" --log-level info --log-file "$LOG" \
    --dump "$WORKDIR/dump.snap" >/dev/null 2>&1 &
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

echo "Running $TESTS against $SERVER on port $PORT"
if ! "$TESTS" --order rand; then
    echo "Integration tests failed. Server log: $LOG" >&2
    exit 1
fi
