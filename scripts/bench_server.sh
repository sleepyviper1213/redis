#!/usr/bin/env bash
# Benchmark SET throughput of redis_server (and optionally a reference
# redis-server) with redis-benchmark, over three scenarios:
#
#   clients  50 parallel clients, no pipelining  (the default redis-benchmark run)
#   latency  1 client, one round trip at a time  (per-request overhead)
#   pipeline 50 clients, 16 commands per batch   (command execution cost)
#   bigget   GET over KEYSPACE random keys, 16 per batch (cache-miss cost; the
#            other scenarios touch one key)
#
#   scripts/bench_server.sh [configure-preset] [configuration] [port]
#
# Server options go in SERVER_ARGS, e.g. the sender-based sessions:
#   SERVER_ARGS="--sender-sessions --io-threads 4" scripts/bench_server.sh
#
# Other knobs: RUNS (repeats per scenario, default 3), REQUESTS (default
# 200000), PIPE_REQUESTS (the pipeline and bigget scenarios, default 8000000),
# KEYSPACE (keys in the bigget scenario, default 2000000), and
# REF_SERVER to also benchmark a reference redis-server binary, e.g.
# REF_SERVER=redis-server (Homebrew's). The script starts its own instance on
# REF_PORT (default 6382) with persistence off and a throwaway directory, so
# it begins empty like redis_server and leaves nothing behind.
#
# The servers take turns: every scenario alternates between them run by run
# (the order flips on each round), so a noisy VM disturbs both alike. Each line
# gives every run, the best (the least disturbed by other processes) and the
# median (the steadier figure to compare).
#
# SERVER_CPUS and CLIENT_CPUS (taskset CPU lists, e.g. 0 and 1-3) pin the
# servers and redis-benchmark to separate CPUs, so the client cannot take the
# server's; unset by default.
#
# THREADS (default 1) runs redis-benchmark with that many threads. A single
# client thread tops out near 1.4M rps on an M-series Mac, which caps the
# pipeline and bigget scenarios before the server does; e.g. THREADS=4. The
# latency scenario stays single-threaded: it has one connection, and so does
# the clients scenario, which a server finishes far below the client's limit.
#
# With --threads redis-benchmark seems to time a run in 250 ms ticks, so a run
# must last seconds for the rate to mean anything: that is why pipeline and
# bigget have their own, larger, request count. Identical rates across runs
# and servers mean the runs were too short.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-linux-gcc}"
CONFIG="${2:-Release}"
PORT="${3:-6381}"
RUNS="${RUNS:-3}"
REQUESTS="${REQUESTS:-200000}"
PIPE_REQUESTS="${PIPE_REQUESTS:-8000000}"
KEYSPACE="${KEYSPACE:-2000000}"
REF_SERVER="${REF_SERVER:-}"
REF_PORT="${REF_PORT:-6382}"
THREADS="${THREADS:-1}"
# Empty for the default, so the runs are exactly what they were before.
CLIENT_THREADS=()
if [[ "$THREADS" -gt 1 ]]; then CLIENT_THREADS=(--threads "$THREADS"); fi

# Empty when unpinned. The "${array[@]+...}" expansions keep bash 3.2 (macOS's
# /bin/bash) from calling an empty array "unbound" under set -u.
SERVER_PIN=()
CLIENT_PIN=()
if [[ -n "${SERVER_CPUS:-}${CLIENT_CPUS:-}" ]]; then
    command -v taskset >/dev/null ||
        { echo "error: SERVER_CPUS/CLIENT_CPUS need taskset" >&2; exit 1; }
    if [[ -n "${SERVER_CPUS:-}" ]]; then SERVER_PIN=(taskset -c "$SERVER_CPUS"); fi
    if [[ -n "${CLIENT_CPUS:-}" ]]; then CLIENT_PIN=(taskset -c "$CLIENT_CPUS"); fi
fi

command -v redis-benchmark >/dev/null ||
    { echo "error: redis-benchmark not found in PATH" >&2; exit 1; }

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
WORKDIR="$(mktemp -d)"

# Cap the server at error level: logging would skew the numbers.
${SERVER_PIN[@]+"${SERVER_PIN[@]}"} "$SERVER" --port "$PORT" \
    --log-level error --log-file "$WORKDIR/server.log" \
    --dump "$WORKDIR/dump.snap" ${SERVER_ARGS:-} >/dev/null 2>&1 &
SERVER_PID=$!
REF_PID=""
if [[ -n "$REF_SERVER" ]]; then
    command -v "$REF_SERVER" >/dev/null ||
        { echo "error: $REF_SERVER not found in PATH" >&2; exit 1; }
    mkdir "$WORKDIR/ref"
    # No snapshots or AOF: a background save would disturb the measurement.
    ${SERVER_PIN[@]+"${SERVER_PIN[@]}"} "$REF_SERVER" --port "$REF_PORT" \
        --save "" --appendonly no \
        --dir "$WORKDIR/ref" --logfile "$WORKDIR/ref.log" >/dev/null 2>&1 &
    REF_PID=$!
fi
cleanup() {
    local status=$?
    if [[ $status -ne 0 ]]; then
        # A reset connection usually means the server died: say so, and show
        # its log before the work directory is removed.
        if kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "note: the server is still running (it closed the connection)" >&2
        else
            echo "error: the server exited during the run" >&2
        fi
        echo "--- last server log lines ---" >&2
        tail -n 20 "$WORKDIR/server.log" >&2 2>/dev/null || true
    fi
    local pid
    for pid in "$SERVER_PID" $REF_PID; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

wait_for_port() {
    local port="$1"
    for _ in $(seq 1 50); do
        redis-benchmark -p "$port" -t ping -n 1 -q >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    echo "error: nothing is listening on port $port" >&2
    return 1
}

SERVER_NAMES=("redis_server ${SERVER_ARGS:-}")
SERVER_PORTS=("$PORT")
if [[ -n "$REF_SERVER" ]]; then
    SERVER_NAMES+=("reference server: $("$REF_SERVER" --version)")
    SERVER_PORTS+=("$REF_PORT")
fi
REPORT=() # per server, its result lines

# one_run <port> <redis-benchmark args...>: requests/second of one run.
one_run() {
    local port="$1"
    shift
    ${CLIENT_PIN[@]+"${CLIENT_PIN[@]}"} \
        redis-benchmark -p "$port" -t "${TEST:-set}" -q "$@" |
        tr '\r' '\n' | awk '/requests per second/ { v = $2 } END { print v }'
}

# scenario <label> <redis-benchmark args...>: RUNS rounds, each running every
# server once, in the opposite order to the round before.
scenario() {
    local label="$1"
    shift
    local results=() order i round best median
    for i in "${!SERVER_PORTS[@]}"; do results[i]=""; done
    for round in $(seq 1 "$RUNS"); do
        order=()
        if (( round % 2 == 0 )); then
            for ((i = ${#SERVER_PORTS[@]} - 1; i >= 0; i--)); do order+=("$i"); done
        else
            for i in "${!SERVER_PORTS[@]}"; do order+=("$i"); done
        fi
        for i in "${order[@]}"; do
            results[i]+="$(one_run "${SERVER_PORTS[i]}" "$@") "
        done
    done
    for i in "${!SERVER_PORTS[@]}"; do
        read -r best median < <(printf '%s\n' ${results[i]} | sort -n |
            awk '{ a[NR] = $1 } END {
                m = (NR % 2) ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2
                print a[NR], m }')
        REPORT[i]+="$(printf '  %-9s %s best %s  median %s rps' \
            "$label" "${results[i]}" "$best" "$median")"$'\n'
    done
}

# Wait for every server first: the scenarios start straight away.
for port in "${SERVER_PORTS[@]}"; do wait_for_port "$port"; done

scenario clients  -n "$REQUESTS"
scenario latency  -n "$((REQUESTS / 2))" -c 1
scenario pipeline -n "$PIPE_REQUESTS" -P 16 \
    ${CLIENT_THREADS[@]+"${CLIENT_THREADS[@]}"}
# A keyspace too big for the CPU caches, where every lookup misses: fill each
# server (random keys, so about 86% of KEYSPACE are hit), then read it back.
for port in "${SERVER_PORTS[@]}"; do
    ${CLIENT_PIN[@]+"${CLIENT_PIN[@]}"} \
        redis-benchmark -p "$port" -t set -q -n "$((KEYSPACE * 2))" \
        -r "$KEYSPACE" -P 16 \
        ${CLIENT_THREADS[@]+"${CLIENT_THREADS[@]}"} >/dev/null
done
TEST=get scenario bigget -n "$PIPE_REQUESTS" -r "$KEYSPACE" -P 16 \
    ${CLIENT_THREADS[@]+"${CLIENT_THREADS[@]}"}

for i in "${!SERVER_PORTS[@]}"; do
    echo "== ${SERVER_NAMES[i]} (port ${SERVER_PORTS[i]}, $REQUESTS requests ($PIPE_REQUESTS pipelined), $RUNS runs, $THREADS client threads) =="
    printf '%s' "${REPORT[i]}"
done
