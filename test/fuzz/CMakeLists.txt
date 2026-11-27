# libFuzzer harnesses (DX-86): the parsers of client input (#51; TC-300,
# TC-307, TC-308), the glob matcher (TC-329), number parsing (TC-330) and the
# replay of an append-only log (TC-331, DX-92).
#
# One harness per entry point, each a LLVMFuzzerTestOneInput(). With
# REDIS_BUILD_FUZZERS it links libFuzzer (cmake/Fuzzing.cmake); otherwise it
# links replay_main.cpp. Either way CTest replays its corpus directories, so
# every input checked in under corpus/ runs in every preset.
#
#     cmake --workflow --preset x86_64-linux-gnu-clang-fuzz
#     scripts/fuzz.sh 60        # fuzz each harness for 60 s

set(REDIS_FUZZ_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/corpus")

# redis_add_fuzzer(<name> LIBS <target>... CORPUS <dir under corpus/>...)
function(redis_add_fuzzer name)
    cmake_parse_arguments(PARSE_ARGV 1 FUZZ "" "" "LIBS;CORPUS")
    set(target redis_fuzz_${name})
    add_executable(${target} ${name}.fuzz.cpp)
    target_link_libraries(${target} PRIVATE ${FUZZ_LIBS})
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

    list(TRANSFORM FUZZ_CORPUS PREPEND "${REDIS_FUZZ_CORPUS}/")
    add_test(NAME fuzz.${name}.corpus
             COMMAND ${target} ${replay_args} ${FUZZ_CORPUS})
endfunction()

redis_add_fuzzer(resp_parser LIBS redis::common CORPUS resp)
redis_add_fuzzer(resp_request LIBS redis::common CORPUS resp)
redis_add_fuzzer(glob LIBS redis::core CORPUS glob)
redis_add_fuzzer(parse_number LIBS redis::core CORPUS number)
redis_add_fuzzer(aof_replay LIBS redis::server_lib CORPUS aof resp)
