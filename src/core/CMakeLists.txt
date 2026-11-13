# Facilities that know nothing of Redis: error handling, logging, time and
# general-purpose containers and utilities. Depends on nothing in this tree.
add_library(
    redis_core
    error/simple_message.cpp
    logging/channels.cpp
    logging/guard.cpp
    logging/lifecycle.cpp
    os/save_file.cpp
    utils/glob.cpp
    utils/parse_number.cpp
    chrono/date_time.cpp
    chrono/deadline_timer.cpp
    chrono/tick_clock.cpp
)

add_library(redis::core ALIAS redis_core)

# The only platform-specific code: processes, file descriptors and fsync.
if(WIN32)
    target_sources(redis_core PRIVATE os/windows.cpp)
    # <windows.h> defines min() and max() macros that break std::min and
    # std::max; every consumer includes it, directly or through Asio.
    target_compile_definitions(redis_core PUBLIC NOMINMAX)
else()
    target_sources(redis_core PRIVATE os/posix.cpp)
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

set_warnings(redis_core)
enable_coverage(redis_core)
