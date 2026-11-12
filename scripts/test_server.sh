#!/usr/bin/env bash
# Run the integration suite against a freshly started server.
#
#   scripts/test_server.sh [configure-preset] [configuration] [port]
#
# Extra server options can be passed in SERVER_ARGS, e.g. to run the same suite
# with several I/O threads:
#   SERVER_ARGS="--io-threads 4" scripts/test_server.sh
#
# The server is then stopped with SIGTERM and must exit cleanly, with nothing
# from a sanitizer on its stderr: with a sanitizer preset, a race or a leak in
# the server fails the run.
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

# shellcheck disable=SC2086 # SERVER_ARGS is a list of words
start_server "$SERVER" "$PORT" "$WORKDIR" ${SERVER_ARGS:-}
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

echo "Running $TESTS against $SERVER on port $PORT ${SERVER_ARGS:-}"
if ! REDIS_TEST_PORT="$PORT" "$TESTS" --order rand; then
    echo "Integration tests failed. Server log: $WORKDIR/redis_server.log" >&2
    exit 1
fi
stop_server "$WORKDIR"
