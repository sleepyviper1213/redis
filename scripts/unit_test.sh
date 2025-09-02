#!/bin/bash
set -euo pipefail

PRESET="${1:-vcpkg-debug}"

# Split preset into name and build type
PRESET_NAME="${PRESET%-*}"        # before last '-'
BUILD_TYPE="${PRESET##*-}"        # after last '-'

# Capitalize first letter of BUILD_TYPE for Ninja Multi-Config
BUILD_TYPE="$(tr '[:lower:]' '[:upper:]' <<< "${BUILD_TYPE:0:1}")${BUILD_TYPE:1}"

# Configurable paths and port
ROOT_DIR="$(pwd)"
$ROOT_DIR/build/$PRESET_NAME/tests/$BUILD_TYPE/redis_unit_test
