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

# MinGW with winpthreads: make every translation unit agree on Asio's thread
# type. Asio uses posix_thread if _POSIX_THREADS is defined when its config
# header is read, and win_thread otherwise, and winpthreads defines it only
# once <pthread.h> is in (through <thread> or <mutex>, say). So the choice
# followed each file's include order, and win_iocp_io_context, which owns its
# timer thread by that type, had two layouts in one program: one file started
# the thread as one type and another joined it as the other, with a garbage
# handle. The result was a crash, an invalid handle or a hang in
# ~io_context, depending on the garbage. Asio has no switch for win_thread,
# but defines BOOST_ASIO_HAS_PTHREADS only if it is not defined already, so
# defining it everywhere settles on posix_thread.
if(MINGW AND CMAKE_USE_PTHREADS_INIT)
    add_compile_definitions(BOOST_ASIO_HAS_PTHREADS=1)
endif()

if(REDIS_BUILD_TESTS)
    find_package(Catch2 3 CONFIG REQUIRED)
    find_package(Hiredis REQUIRED)
endif()
