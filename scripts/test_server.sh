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
REDIS_TEST="$ROOT_DIR/build/$PRESET_NAME/tests/$BUILD_TYPE/redis_server_test"
REDIS_SERVER="$ROOT_DIR/build/$PRESET_NAME/src/$BUILD_TYPE/redis_server"
PORT="${2:-6380}"

# Build the project
cmake --build --preset $PRESET

# Clean old logs
rm -f server_test.log server_test.err

# Check for stale server process on port
STALE_PID=$(lsof -t -i:"$PORT" || true)
if [ -n "$STALE_PID" ]; then
    echo "Killing stale server process (PID: $STALE_PID)"
    if ! kill "$STALE_PID" 2>/dev/null; then
        echo "Warning: Failed to kill stale PID $STALE_PID"
    fi
    sleep 1 # Wait for port to be released
fi

# Start the server with redirected output
"$REDIS_SERVER" --log-level trace --port "$PORT" &
SERVER_PID=$!
echo "Server started with PID: $SERVER_PID"

# Wait for server to start (poll port for up to 10 seconds)
MAX_ATTEMPTS=100 # 10 seconds / 0.1 seconds per attempt
ATTEMPT=0
until nc -z 127.0.0.1 "$PORT" || [ $ATTEMPT -ge $MAX_ATTEMPTS ]; do
    sleep 0.1
    ATTEMPT=$((ATTEMPT + 1))
    # Log partial errors for debugging
    if [ $ATTEMPT -eq 50 ]; then
        echo "Server not up after 5 seconds, partial errors:"
        head -n 10 server_test.err
    fi
done

if ! nc -z 127.0.0.1 "$PORT"; then
    echo "Error: Server failed to start after 10 seconds. Check server_test.err:"
    cat server_test.err
    exit 1
fi

# Run Catch2 tests
echo "Running tests: $REDIS_TEST"
if "$REDIS_TEST"; then
    echo "Tests passed. Stopping server (PID: $SERVER_PID)"
    kill "$SERVER_PID" 2>/dev/null || true
    exit 0
else
    TEST_EXIT_CODE=$?
    echo "Tests failed with exit code $TEST_EXIT_CODE. Server (PID: $SERVER_PID) is still running for debugging."
    echo "Check redis_server.log for server errors:"
    exit $TEST_EXIT_CODE
fi

