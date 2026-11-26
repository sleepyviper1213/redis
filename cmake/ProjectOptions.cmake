include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(REDIS_BUILD_TESTS "Build unit and integration tests" ${PROJECT_IS_TOP_LEVEL})
option(REDIS_INSTALL "Generate the install target" ${PROJECT_IS_TOP_LEVEL})
option(REDIS_ENABLE_CCACHE
       "Use ccache as the compiler launcher when found" ${PROJECT_IS_TOP_LEVEL})
option(REDIS_ENABLE_IPO
       "Enable IPO/LTO on the Release and RelWithDebInfo configurations" OFF)
option(REDIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(REDIS_ENABLE_COVERAGE "Instrument for coverage reporting" OFF)
option(REDIS_ENABLE_STATIC_ANALYZERS "Run clang-tidy and cppcheck" OFF)
option(REDIS_ENABLE_CLANG_TIDY "Run clang-tidy only" OFF)
option(REDIS_ENABLE_CPPCHECK "Run cppcheck only" OFF)
option(REDIS_USE_JEMALLOC
       "Allocate the keyspace with jemalloc (Linux and macOS; needs the vcpkg feature jemalloc)" OFF)
option(REDIS_BUILD_FUZZERS
       "Link test/fuzz with libFuzzer and instrument the code for it and UBSan (Clang only; see cmake/Fuzzing.cmake)" OFF)
option(REDIS_WARN_UNSAFE_BUFFERS
       "Enable Clang -Wunsafe-buffer-usage (noisy; never promoted to error)" OFF)

# ON / fast    → enable with libc++ fast mode (recommended default)
#
# Every level leaves the Release configuration unhardened (it is the one that
# is benchmarked); all other configurations are hardened.
#
# Hardening does NOT undefine NDEBUG: a raw assert() stays compiled out of a
# configuration that defines it. The checks that remain come from the standard
# library (_GLIBCXX_ASSERTIONS, _LIBCPP_HARDENING_MODE), _FORTIFY_SOURCE and the
# compiler and linker flags. See Hardening.cmake.
set(REDIS_HARDENING
    "ON"
    CACHE STRING "Hardening level: OFF | ON | none | fast | extensive | debug")
set_property(CACHE REDIS_HARDENING PROPERTY STRINGS OFF ON none fast extensive debug)

option(REDIS_HARDENING_UBSAN_TRAP
       "Trap on undefined behaviour instead of continuing (Clang/GCC)" OFF)

set(REDIS_SANITIZER
    "OFF"
    CACHE STRING
          "Which sanitizer configs to register (one or more, ';'-separated): Address | Thread | Undefined | Realtime")
set_property(CACHE REDIS_SANITIZER PROPERTY STRINGS OFF Address Thread Undefined Realtime)

# ---------------------------------------------------------------------------
# Language / visibility / build type
# ---------------------------------------------------------------------------

if(PROJECT_IS_TOP_LEVEL
   AND NOT CMAKE_CONFIGURATION_TYPES
   AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        Release
        CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release
                                                 RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# GNU extensions stay on: the REDIS_TRY/REDIS_TRY_VALUE error-propagation macros in
# core/error/try.hpp are built on statement-expressions ({ ... }).
set(CMAKE_CXX_EXTENSIONS ON)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    set(CMAKE_COLOR_DIAGNOSTICS ON)
endif()

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(
        FATAL_ERROR
            "
        In-source builds are strictly prohibited!
        Please clear the generated files (e.g., CMakeCache.txt) and use an out-of-source build:
        cmake -S . -B build
    ")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/CCache.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/InterProceduralOptimisation.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Sanitizers.cmake)

if(REDIS_HARDENING_UBSAN_TRAP AND REDIS_SANITIZER)
    message(
        WARNING
            "REDIS_HARDENING_UBSAN_TRAP plus sanitizer configs both enable UBSan. "
            "Trap flags are suppressed under sanitizer configs.")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/StaticAnalyzers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Warnings.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Hardening.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Coverage.cmake)
