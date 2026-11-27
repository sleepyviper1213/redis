#!/usr/bin/env bash
# Fuzz each harness of test/fuzz for a fixed time (DX-86, #51).
#
#     cmake --workflow --preset x86_64-linux-gnu-clang-fuzz
#     scripts/fuzz.sh [seconds per harness, default 60]
#
# New inputs go to build/<preset>/fuzz-work/<harness>/; the checked-in corpus
# is only read. A crashing input is written to fuzz-artifacts/ and the script
# fails. Minimise it (-minimize_crash=1), add it to the harness's directory
# under test/fuzz/corpus/ and fix the bug: from then on CTest replays it in
# every preset.
set -euo pipefail
cd "$(dirname "$0")/.."

seconds="${1:-60}"
preset=x86_64-linux-gnu-clang-fuzz
bin_dir="build/$preset/test/fuzz/AddressSanitizer"
artifacts="fuzz-artifacts"
mkdir -p "$artifacts"

# -malloc_limit_mb: no single allocation may exceed 64 MiB, far above what a
# 64 KiB input may make the parsers allocate (TC-300). The glob and number
# harnesses take 4 KiB: the glob reference is quadratic, and no number is longer.
export ASAN_OPTIONS="abort_on_error=1:halt_on_error=1:detect_stack_use_after_return=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

# harness  corpus directories (under test/fuzz/corpus)  dictionary  max input
harnesses=(
    "resp_parser   resp      resp.dict   65536"
    "resp_request  resp      resp.dict   65536"
    "glob          glob      glob.dict   4096"
    "parse_number  number    number.dict 4096"
    "aof_replay    aof,resp  resp.dict   65536"
)

for row in "${harnesses[@]}"; do
    read -r harness corpora dict max_len <<<"$row"
    binary="$bin_dir/redis_fuzz_$harness"
    if [ ! -x "$binary" ]; then
        echo "error: $binary not found. Build it first: cmake --workflow --preset $preset" >&2
        exit 1
    fi
    work="build/$preset/fuzz-work/$harness"
    mkdir -p "$work"
    corpus_dirs=()
    IFS=, read -ra names <<<"$corpora"
    for name in "${names[@]}"; do corpus_dirs+=("test/fuzz/corpus/$name"); done
    echo "== $harness: ${seconds}s"
    "$binary" "$work" "${corpus_dirs[@]}" \
        -dict="test/fuzz/$dict" \
        -max_total_time="$seconds" \
        -max_len="$max_len" \
        -malloc_limit_mb=64 \
        -rss_limit_mb=2048 \
        -timeout=10 \
        -print_final_stats=1 \
        -artifact_prefix="$artifacts/$harness-"
done
