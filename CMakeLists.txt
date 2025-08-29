cmake_minimum_required(VERSION 3.28)

project(
    Redis
    LANGUAGES CXX
    VERSION 1.0)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(NOT DEFINED FMT_MASTER_PROJECT)
    set(FMT_MASTER_PROJECT OFF)
    if(PROJECT_IS_TOP_LEVEL)
        set(FMT_MASTER_PROJECT ON)
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    endif()
    message(STATUS "CMake version: ${CMAKE_VERSION}")
endif()

# guard against in-source builds
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(
        FATAL_ERROR
            "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there. You may need to remove CMakeCache.txt. "
    )
endif()

find_package(Catch2 REQUIRED)
find_package(fmt 11 REQUIRED)
find_package(spdlog REQUIRED)
find_package(cereal REQUIRED)
find_package(CLI11 REQUIRED)
find_package(boost_asio QUIET)
find_package(magic_enum REQUIRED)
find_package(FastFloat REQUIRED)

if(boost_asio_FOUND)
    message(STATUS "Using standalone Boost asio")
else()
    find_package(Boost CONFIG COMPONENTS system thread)
    message(STATUS "Using classic Boost")
endif()
# --- Options / Macros ---
option(REDIS_DOC "Generate the doc target." ${REDIS_MASTER_PROJECT})
option(REDIS_INSTALL "Generate the install target." ON)
option(REDIS_TEST "Generate the test target." ${REDIS_MASTER_PROJECT})

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Let's ensure -std=g++xx instead of -std=c++xx
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)

# configure files based on CMake configuration options
# add_subdirectory(configured_files)

if(REDIS_INSTALL)
    # --- Package export setup ---
    include(CMakePackageConfigHelpers)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/redisConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageConfigHelpers.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/redisConfig.cmake"
        INSTALL_DESTINATION lib/cmake/redis)

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/redisConfig.cmake"
                  "${CMAKE_CURRENT_BINARY_DIR}/redisConfigVersion.cmake"
            DESTINATION lib/cmake/redis)
endif()

add_subdirectory(src)

if(REDIS_MASTER_PROJECT)
    # Testing only available if this is the main app.
    add_subdirectory(tests)
endif()
