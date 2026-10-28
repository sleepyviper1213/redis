#!/usr/bin/env bash
# Start the server from a preset's build tree.
#
#   scripts/run.sh [configure-preset] [configuration] [-- server args...]
#   scripts/run.sh linux-gcc Release -- --port 6380 --log-level info
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-Debug}"
shift $(( $# < 2 ? $# : 2 ))
[ "${1:-}" = "--" ] && shift

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
exec "$SERVER" --log-level trace "$@"
