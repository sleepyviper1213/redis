cmake_minimum_required(VERSION 3.28)

project(Redis LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
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

if (boost_asio_FOUND)
    message(STATUS "Using standalone Boost asio")
else()
	find_package(Boost CONFIG COMPONENTS system thread)
    message(STATUS "Using classic Boost")
endif()

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  # Let's ensure -std=g++xx instead of -std=c++xx
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_EXTENSIONS ON)
  # Testing only available if this is the main app.
  add_subdirectory(tests)
endif()

# configure files based on CMake configuration options
# add_subdirectory(configured_files)

add_subdirectory(src)
