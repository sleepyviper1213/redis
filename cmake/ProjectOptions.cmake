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
option(REDIS_WARN_UNSAFE_BUFFERS
       "Enable Clang -Wunsafe-buffer-usage (noisy; never promoted to error)" OFF)

set(REDIS_SANITIZER
    "OFF"
    CACHE STRING
          "Which sanitizer config to register (exactly one): Address | Thread | Undefined")
set_property(CACHE REDIS_SANITIZER PROPERTY STRINGS OFF Address Thread Undefined)

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
# GNU extensions stay on: the TRY/TRY_VALUE error-propagation macros in
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
include(${CMAKE_CURRENT_LIST_DIR}/StaticAnalyzers.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Warnings.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Coverage.cmake)
