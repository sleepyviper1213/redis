#!/usr/bin/env bash
# Key expiry across a server restart: the scenarios that need one server to stop
# and another to start over its files (docs/features/key_expiry/test_plan.md,
# TC-117, TC-118, TC-122; issue #43).
#
#   scripts/test_expiry_server.sh [configure-preset] [configuration] [port]
#
# 1. A server at trace level: the write phase of redis_integration_test lets keys
#    expire by the sweep, then saves keys with deadlines.
# 2. The server stops; its log must say "Expired N keys" and never a key name.
# 3. The script waits until a short deadline has passed, with no server running.
# 4. A new server over the same snapshot: the verify phase checks the deadlines.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-x86_64-linux-gnu-gcc}"
CONFIG="${2:-Debug}"
PORT="${3:-6385}"

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
TESTS="$(locate_binary "$PRESET" "$CONFIG" redis_integration_test)"
WORKDIR="$(mktemp -d)"
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

# shellcheck disable=SC2086 # SERVER_ARGS is a list of words
SERVER_LOG_LEVEL=trace start_server "$SERVER" "$PORT" "$WORKDIR" ${SERVER_ARGS:-}
echo "Write phase"
REDIS_TEST_PORT="$PORT" "$TESTS" "[restart-write]"
stop_server "$WORKDIR"

# Asynchronous logging is flushed when the server stops. Where the server can
# only be killed (Windows) that happens a moment after stop_server returns, so
# wait for the line rather than look once.
LOG="$WORKDIR/redis_server.log"
for _ in $(seq 50); do
    grep -q -E "Expired [0-9]+ keys" "$LOG" 2>/dev/null && break
    sleep 0.1
done
if ! grep -q -E "Expired [0-9]+ keys" "$LOG"; then
    echo "error: the sweep logged no \"Expired N keys\" line (TC-122)." >&2
    tail -n 30 "$LOG" >&2
    exit 1
fi
if grep -q "secret_expiring" "$LOG"; then
    echo "error: a key name is in the server log (TC-122)." >&2
    exit 1
fi

# The short deadline is 1.5 s; the server has been down for part of it.
sleep 2

# shellcheck disable=SC2086
start_server "$SERVER" "$PORT" "$WORKDIR" ${SERVER_ARGS:-}
echo "Verify phase"
REDIS_TEST_PORT="$PORT" "$TESTS" "[restart-verify]"
stop_server "$WORKDIR"
echo "Key expiry across a restart: passed"
