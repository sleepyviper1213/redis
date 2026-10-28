cmake_minimum_required(VERSION 3.28)

project(
    redis
    VERSION 1.0.0
    DESCRIPTION "A Redis-compatible key-value server built on C++23 coroutines"
    LANGUAGES CXX)

# The server forks for background saves and fsyncs snapshots: it targets
# Linux and macOS. On Windows, develop through WSL.
if(NOT UNIX)
    message(FATAL_ERROR "redis targets Unix-like systems (Linux, macOS). "
                        "On Windows, build inside WSL.")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(cmake/ProjectOptions.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(REDIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(REDIS_INSTALL)
    include(cmake/Install.cmake)
endif()
