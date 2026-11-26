# Fuzz harnesses for the parsers of client input (DX-86, #51; TC-300, TC-307,
# TC-308 in docs/features/connection/test_plan.md).
#
# One harness per entry point, each a LLVMFuzzerTestOneInput(). With
# REDIS_BUILD_FUZZERS it links libFuzzer (cmake/Fuzzing.cmake); otherwise it
# links replay_main.cpp. Either way CTest replays corpus/, so every input
# checked in there runs in every preset.
#
#     cmake --workflow --preset x86_64-linux-gnu-clang-fuzz
#     scripts/fuzz.sh 60        # fuzz each harness for 60 s

set(REDIS_FUZZ_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/corpus")

function(redis_add_fuzzer name)
    set(target redis_fuzz_${name})
    add_executable(${target} ${name}.fuzz.cpp)
    target_link_libraries(${target} PRIVATE redis::common)
    set_warnings(${target})
    copy_sanitizer_runtime(${target})

    if(REDIS_BUILD_FUZZERS)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer)
        # Run every corpus file once, then exit.
        set(replay_args -runs=0)
    else()
        target_sources(${target} PRIVATE replay_main.cpp)
        set(replay_args)
    endif()

    add_test(NAME fuzz.${name}.corpus
             COMMAND ${target} ${replay_args} ${REDIS_FUZZ_CORPUS})
endfunction()

redis_add_fuzzer(resp_parser)
redis_add_fuzzer(resp_request)
