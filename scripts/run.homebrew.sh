#!/bin/sh
set -euo pipefail

PRESET="homebrew"
cmake --build --preset $PRESET

# Capitalize first letter of build type (Debug, Release) in a portable way
ROOT_DIR="$(pwd)"
REDIS_SERVER="$ROOT_DIR/build/$PRESET/src/redis_server"
"$REDIS_SERVER" --log-level trace
