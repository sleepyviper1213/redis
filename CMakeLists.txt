cmake_minimum_required(VERSION 3.28)

project(Redis LANGUAGES CXX VERSION 1.0)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
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

if (boost_asio_FOUND)
    message(STATUS "Using standalone Boost asio")
else()
	find_package(Boost CONFIG COMPONENTS system thread)
    message(STATUS "Using classic Boost")
endif()
# --- Options / Macros ---
option(REDIS_BUILD_TESTS "Build tests" OFF)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Let's ensure -std=g++xx instead of -std=c++xx
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS ON)
# --- Package export setup ---
include(CMakePackageConfigHelpers)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  # Testing only available if this is the main app.
  add_subdirectory(tests)
endif()

# configure files based on CMake configuration options
# add_subdirectory(configured_files)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/redis_serverConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageConfigHelpers.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/redis_serverConfig.cmake"
        INSTALL_DESTINATION lib/cmake/redis_server
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/redis_serverConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/redis_serverConfigVersion.cmake"
        DESTINATION lib/cmake/redis_server
)
add_subdirectory(src)
