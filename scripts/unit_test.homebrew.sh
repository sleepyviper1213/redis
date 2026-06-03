#!/bin/sh
set -euo pipefail

PRESET="homebrew"
cmake --build --preset $PRESET

# Capitalize first letter of build type (Debug, Release) in a portable way
ROOT_DIR="$(pwd)"
"$ROOT_DIR/build/$PRESET/tests/redis_test_lib"
