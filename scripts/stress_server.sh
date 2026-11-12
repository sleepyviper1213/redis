#!/usr/bin/env bash
# Run the verifying stress test against a freshly started server.
#
#   scripts/stress_server.sh [configure-preset] [configuration] [port]
#
# Server options go in SERVER_ARGS, stress options in STRESS_ARGS, e.g. to
# stress several I/O threads:
#   SERVER_ARGS="--io-threads 4" \
#   STRESS_ARGS="--clients 64 --ops 50000 --pipeline 64" \
#       scripts/stress_server.sh
#
# A failing run prints its seed; replay it with STRESS_ARGS="--seed <n>".
# The server is then stopped with SIGTERM and must exit cleanly, with nothing
# from a sanitizer on its stderr: with a ThreadSanitizer preset, a data race
# the replies alone would not show fails the run.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-Debug}"
PORT="${3:-6381}"

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
STRESS="$(locate_binary "$PRESET" "$CONFIG" redis_stress_test)"
WORKDIR="$(mktemp -d)"

# shellcheck disable=SC2086 # SERVER_ARGS is a list of words
start_server "$SERVER" "$PORT" "$WORKDIR" ${SERVER_ARGS:-}
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

echo "Running $STRESS against $SERVER on port $PORT ${SERVER_ARGS:-}"
# shellcheck disable=SC2086 # STRESS_ARGS is a list of words
if ! "$STRESS" --port "$PORT" ${STRESS_ARGS:-}; then
    echo "Stress test failed. Server log: $WORKDIR/redis_server.log" >&2
    exit 1
fi
stop_server "$WORKDIR"
