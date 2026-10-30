# The server: command execution, the typed keyspace, snapshot persistence and
# the TCP front end.
# Everything except main() is a library so the command and session logic can be
# unit tested without opening sockets.
add_library(
    redis_server_lib STATIC
    commands/handler.cpp
    commands/hash.cpp
    commands/keyspace.cpp
    commands/list.cpp
    commands/server.cpp
    commands/set.cpp
    commands/string.cpp

    net/executor.cpp
    net/pipeline.cpp
    net/redis_server.cpp
    net/session.cpp
    net/tcp_server.cpp
    net/v2/session.cpp
    net/v2/tcp_server.cpp

    persistence/persistence.cpp
    persistence/snapshot.cpp

    storage/database.cpp

    setup_cli_options.cpp)

add_library(redis::server_lib ALIAS redis_server_lib)

if(WIN32)
    target_sources(redis_server_lib PRIVATE persistence/windows.cpp)
    # Asio's Windows sockets; 0x0A00 is Windows 10.
    target_compile_definitions(redis_server_lib PUBLIC _WIN32_WINNT=0x0A00)
    target_link_libraries(redis_server_lib PUBLIC ws2_32 mswsock)
else()
    target_sources(redis_server_lib PRIVATE persistence/posix.cpp)
endif()

target_compile_definitions(redis_server_lib PUBLIC BOOST_ASIO_NO_DEPRECATED)
target_link_libraries(redis_server_lib PUBLIC redis::core CLI11::CLI11
    STDEXEC::stdexec
    Threads::Threads
    PRIVATE cereal::cereal)

if(boost_asio_FOUND)
    target_link_libraries(redis_server_lib PUBLIC Boost::asio)
else()
    target_link_libraries(redis_server_lib PUBLIC Boost::thread Boost::system)
endif()

# Keyspace hash map: boost (default), absl or std. Build each into its own
# tree to compare them, e.g. cmake --preset <p> -DREDIS_KEYSPACE_MAP=absl
set(REDIS_KEYSPACE_MAP "boost" CACHE STRING "Keyspace map: boost, absl or std")
set_property(CACHE REDIS_KEYSPACE_MAP PROPERTY STRINGS boost absl std)
if(REDIS_KEYSPACE_MAP STREQUAL "absl")
    find_package(absl CONFIG REQUIRED)
    target_link_libraries(redis_server_lib PUBLIC absl::flat_hash_map absl::hash)
    target_compile_definitions(redis_server_lib PUBLIC REDIS_KEYSPACE_MAP_ABSL)
elseif(REDIS_KEYSPACE_MAP STREQUAL "std")
    target_compile_definitions(redis_server_lib PUBLIC REDIS_KEYSPACE_MAP_STD)
elseif(NOT REDIS_KEYSPACE_MAP STREQUAL "boost")
    message(FATAL_ERROR "REDIS_KEYSPACE_MAP must be boost, absl or std")
endif()

set_warnings(redis_server_lib)
enable_coverage(redis_server_lib)

add_executable(redis_server main.cpp)
add_executable(redis::server ALIAS redis_server)
target_link_libraries(redis_server PRIVATE redis::server_lib)
set_warnings(redis_server)
copy_sanitizer_runtime(redis_server)
