include_guard(GLOBAL)

find_package(fmt 11 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(FastFloat CONFIG REQUIRED)
find_package(cereal CONFIG REQUIRED)
find_package(stdexec CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Prefer standalone Boost.Asio (vcpkg's boost-asio port); fall back to a full
# Boost install, where Asio comes with Boost.System.
find_package(boost_asio CONFIG QUIET)
if(boost_asio_FOUND)
    message(STATUS "Using standalone Boost asio")
else()
    find_package(Boost CONFIG REQUIRED COMPONENTS system thread)
    message(STATUS "Using classic Boost")
endif()

if(REDIS_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)
    find_package(Hiredis REQUIRED)
endif()
