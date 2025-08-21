add_library(
    redis_server_lib
    # actions/db.cpp
    # actions/snapshot.cpp
    # actions/gate.cpp
    commands/cmd.cpp
    commands/cmd.cpp
    commands/command_type.cpp
    config.cpp
    error/error.cpp
        database.cpp
    net/session.cpp
    net/tcp_server.cpp
        resp/parser.cpp
    resp/value.cpp
    utils/create_temp_file.cpp
    utils/get_class_logger.cpp
    utils/toupper.cpp)

target_include_directories(redis_server_lib PUBLIC .)
target_link_libraries(redis_server_lib PUBLIC spdlog::spdlog CLI11::CLI11
                                              magic_enum::magic_enum)

if(boost_asio_FOUND)
    target_link_libraries(redis_server_lib PUBLIC Boost::asio)
else()
    target_link_libraries(redis_server_lib PUBLIC Boost::thread Boost::system)
endif()

target_compile_definitions(redis_server_lib PUBLIC BOOST_ASIO_NO_DEPRECATED)

target_compile_options(redis_server_lib PUBLIC -fsanitize=address)
target_link_options(redis_server_lib PUBLIC -fsanitize=address)

add_executable(redis_server main.cpp)
target_link_libraries(redis_server PUBLIC redis_server_lib)
