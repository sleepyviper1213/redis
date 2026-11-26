include_guard(GLOBAL)

# libFuzzer builds of test/fuzz (DX-86, #51).
#
# With REDIS_BUILD_FUZZERS=ON every target of the project is compiled with
# coverage feedback for libFuzzer (-fsanitize=fuzzer-no-link) and with UBSan,
# so the parsers the harnesses call are instrumented, not only the harnesses.
# Each harness then links -fsanitize=fuzzer (test/fuzz/CMakeLists.txt).
# AddressSanitizer comes from the configuration (REDIS_SANITIZER=Address).
#
# With it OFF, the harnesses link a replay driver instead and run the corpus
# only, on every compiler, so a crashing input checked in stays a regression
# test on Windows too.

if(NOT REDIS_BUILD_FUZZERS)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(
        FATAL_ERROR
            "REDIS_BUILD_FUZZERS needs Clang with the GNU command line; "
            "other compilers build the harnesses with the replay driver.")
endif()

add_compile_options(-fsanitize=fuzzer-no-link,undefined
                    -fno-sanitize-recover=undefined)
add_link_options(-fsanitize=undefined)
message(STATUS "fuzzing: libFuzzer harnesses, code built with fuzzer-no-link and UBSan")
