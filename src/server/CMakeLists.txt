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

    persistence/persistence.cpp
    persistence/posix.cpp
    persistence/snapshot.cpp

    storage/database.cpp

    setup_cli_options.cpp)

add_library(redis::server_lib ALIAS redis_server_lib)

target_compile_definitions(redis_server_lib PUBLIC BOOST_ASIO_NO_DEPRECATED)
target_link_libraries(redis_server_lib PUBLIC redis::core CLI11::CLI11
                                              Threads::Threads
                                       PRIVATE cereal::cereal)

if(boost_asio_FOUND)
    target_link_libraries(redis_server_lib PUBLIC Boost::asio)
else()
    target_link_libraries(redis_server_lib PUBLIC Boost::thread Boost::system)
endif()

set_warnings(redis_server_lib)
enable_coverage(redis_server_lib)

add_executable(redis_server main.cpp)
add_executable(redis::server ALIAS redis_server)
target_link_libraries(redis_server PRIVATE redis::server_lib)
set_warnings(redis_server)
copy_sanitizer_runtime(redis_server)
