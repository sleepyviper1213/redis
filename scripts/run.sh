#!/bin/sh
set -euo pipefail

PRESET="vcpkg"
BUILD_TYPE="debug"

# Build using CMake preset
cmake --build --preset "${PRESET}-${BUILD_TYPE}"

# Capitalize first letter of build type (Debug, Release) in a portable way
BUILD_TYPE_CAP="$(echo "$BUILD_TYPE" | cut -c1 | tr '[:lower:]' '[:upper:]')$(echo "$BUILD_TYPE" | cut -c2-)"

ROOT_DIR="$(pwd)"
BUILD_DIR="$ROOT_DIR/build/$PRESET/src/$BUILD_TYPE_CAP"
REDIS_SERVER="$BUILD_DIR/redis_server"

"$REDIS_SERVER" --log-level trace
