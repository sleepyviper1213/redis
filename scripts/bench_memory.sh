#!/usr/bin/env bash
# Resident memory of redis_server (and optionally a reference redis-server)
# through a fill, a mass delete and a refill with a different value size, the
# pattern that fragments a C library heap (TC-276, ADR-0011).
#
#   scripts/bench_memory.sh [configure-preset] [configuration] [port]
#
#   fill    KEYS random keys each of 16, 200 and 2000-byte values
#   delete  most of the 2000-byte keys, half of the 200-byte ones
#   refill  700-byte values over new keys: they go into the freed space
#
# After each phase it prints the server's resident set (VmRSS, from /proc), the
# memory it says it holds (INFO used_memory) and their ratio. Compare the
# ratio after "refill" between a build with the C library's allocator and one
# with jemalloc. The reference server counts every allocation and redis_server
# only the keyspace, so across the two servers the RSS is the figure to read,
# not the ratio.
#
# Knobs: KEYS (default 200000), REF_SERVER (a redis-server binary to measure
# beside it, default none), SERVER_ARGS (extra options of redis_server).
# Linux only: VmRSS comes from /proc.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/common.sh

PRESET="${1:-x86_64-linux-gnu-gcc}"
CONFIG="${2:-Release}"
PORT="${3:-6381}"
KEYS="${KEYS:-200000}"
REF_SERVER="${REF_SERVER:-}"
REF_PORT="${REF_PORT:-6382}"

for tool in redis-benchmark redis-cli; do
    command -v "$tool" >/dev/null ||
        { echo "error: $tool not found in PATH" >&2; exit 1; }
done
[[ -r /proc/self/status ]] ||
    { echo "error: /proc is needed for VmRSS (Linux only)" >&2; exit 1; }

SERVER="$(locate_binary "$PRESET" "$CONFIG" redis_server)"
WORKDIR="$(mktemp -d)"

"$SERVER" --port "$PORT" --log-level error --log-file "$WORKDIR/server.log" \
    --dump "$WORKDIR/dump.snap" ${SERVER_ARGS:-} >/dev/null 2>&1 &
SERVER_PID=$!
REF_PID=""
if [[ -n "$REF_SERVER" ]]; then
    command -v "$REF_SERVER" >/dev/null ||
        { echo "error: $REF_SERVER not found in PATH" >&2; exit 1; }
    mkdir "$WORKDIR/ref"
    "$REF_SERVER" --port "$REF_PORT" --save "" --appendonly no \
        --dir "$WORKDIR/ref" --logfile "$WORKDIR/ref.log" >/dev/null 2>&1 &
    REF_PID=$!
fi
cleanup() {
    local pid
    for pid in "$SERVER_PID" $REF_PID; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

wait_for_port() {
    for _ in $(seq 1 50); do
        redis-cli -p "$1" ping >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    echo "error: nothing is listening on port $1" >&2
    return 1
}

# rss_kib <pid>
rss_kib() { awk '/^VmRSS:/ { print $2 }' "/proc/$1/status"; }

# used_bytes <port>: INFO used_memory.
used_bytes() {
    redis-cli -p "$1" INFO memory | tr -d '\r' |
        awk -F: '$1 == "used_memory" { print $2 }'
}

# value <bytes>: that many 'x'.
value() { head -c "$1" /dev/zero | tr '\0' x; }

# load <port> <count> <command...>: the command repeated count times
# with __rand_int__ drawn from 0..KEYS-1, 16 to a batch.
load() {
    local port="$1" count="$2"
    shift 2
    redis-benchmark -p "$port" -q -n "$count" -r "$KEYS" -P 16 "$@" >/dev/null
}

V16="$(value 16)"
V200="$(value 200)"
V2000="$(value 2000)"
V700="$(value 700)"

# phase <port> <pid> <label>: sample after a short wait for the allocator.
ROWS=()
phase() {
    local port="$1" pid="$2" label="$3" rss used
    sleep 2
    rss="$(rss_kib "$pid")"
    used="$(used_bytes "$port")"
    ROWS+=("$(awk -v l="$label" -v r="$rss" -v u="$used" 'BEGIN {
        printf "  %-8s rss %8.1f MiB  used_memory %8.1f MiB  rss/used %.2f\n",
               l, r / 1024, u / 1048576, (u > 0 ? r * 1024 / u : 0) }')")
}

run_one() {
    local name="$1" port="$2" pid="$3"
    ROWS=()
    wait_for_port "$port"
    phase "$port" "$pid" empty
    # About 86% of each key space is hit by 2*KEYS random draws.
    load "$port" "$((KEYS * 2))" SET "s:__rand_int__" "$V16"
    load "$port" "$((KEYS * 2))" SET "m:__rand_int__" "$V200"
    load "$port" "$((KEYS * 2))" SET "l:__rand_int__" "$V2000"
    phase "$port" "$pid" fill
    # 4*KEYS draws reach about 98% of the large keys, KEYS about 63% of the
    # medium ones.
    load "$port" "$((KEYS * 4))" DEL "l:__rand_int__"
    load "$port" "$KEYS" DEL "m:__rand_int__"
    phase "$port" "$pid" delete
    load "$port" "$((KEYS * 2))" SET "r:__rand_int__" "$V700"
    phase "$port" "$pid" refill
    echo "== $name (port $port, $KEYS keys per size) =="
    printf '%s\n' "${ROWS[@]}"
}

run_one "redis_server ${SERVER_ARGS:-}" "$PORT" "$SERVER_PID"
if [[ -n "$REF_SERVER" ]]; then
    run_one "reference server: $("$REF_SERVER" --version)" "$REF_PORT" "$REF_PID"
fi
