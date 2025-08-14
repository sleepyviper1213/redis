add_library(
    redis_server_lib
    # actions/db.cpp
    # actions/snapshot.cpp
    # actions/gate.cpp
    # commands/cmd.cpp
    # commands/cmd.cpp
    # commands/command_type.cpp
    error/error.cpp
    net/redis_store.cpp
	net/co_main.cpp
        config.cpp
    net/session.cpp
    net/tcp_server.cpp
    resp/resp_parser.cpp
    resp/value.cpp
    utils/create_temp_file.cpp
        utils/toupper.cpp)


target_include_directories(redis_server_lib PUBLIC .)
target_link_libraries(redis_server_lib PUBLIC spdlog::spdlog Boost::system
	Boost::thread cereal::cereal CLI11::CLI11)
target_compile_definitions(redis_server_lib PUBLIC BOOST_ASIO_NO_DEPRECATED)
target_compile_options(redis_server_lib PUBLIC -fsanitize=address)
target_link_options(redis_server_lib PUBLIC -fsanitize=address)

add_executable(redis_server main.cpp)
target_link_libraries(redis_server PUBLIC redis_server_lib)
