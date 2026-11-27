# Facilities that know nothing of Redis: error handling, logging, time and
# general-purpose containers and utilities. Depends on nothing in this tree.
add_library(
    redis_core
    error/simple_message.cpp
    logging/channels.cpp
    logging/guard.cpp
    logging/lifecycle.cpp
    os/sandbox.cpp
    os/save_file.cpp
    utils/glob.cpp
    utils/parse_number.cpp
    chrono/date_time.cpp
)

add_library(redis::core ALIAS redis_core)

# The only platform-specific code: processes, file descriptors, fsync, the
# sandbox, and macOS's tick counter (docs/design/patterns.md, platform code).
if(APPLE)
    target_sources(redis_core PRIVATE os/tick_clock_mach.cpp)
else()
    target_sources(redis_core PRIVATE chrono/tick_clock.cpp)
endif()

if(WIN32)
    target_sources(redis_core PRIVATE os/windows.cpp)
    # <windows.h> defines min() and max() macros that break std::min and
    # std::max; every consumer includes it, directly or through Asio.
    target_compile_definitions(redis_core PUBLIC NOMINMAX)
else()
    # sandbox_linux.cpp compiles to nothing outside Linux x86-64.
    target_sources(redis_core PRIVATE os/posix.cpp os/sandbox_linux.cpp)
endif()

target_include_directories(
    redis_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# fmt is public: its headers (common/resp/value.hpp, ...) include <fmt/...> and every
# consumer formats with it. Declared here rather than left to spdlog's own link
# list, which only carries it while spdlog is built against external fmt.
target_link_libraries(redis_core PUBLIC fmt::fmt spdlog::spdlog
    reflectcpp::reflectcpp)
target_link_libraries(redis_core PRIVATE FastFloat::fast_float)

# The keyspace allocator is chosen in common/store/allocator.hpp from this
# definition, so every target that sees the allocator must see it too: public.
if(REDIS_USE_JEMALLOC)
    target_compile_definitions(redis_core PUBLIC REDIS_HAS_JEMALLOC=1)
    target_link_libraries(redis_core PUBLIC PkgConfig::JEMALLOC)
endif()

set_warnings(redis_core)
enable_hardening(redis_core)
enable_coverage(redis_core)
