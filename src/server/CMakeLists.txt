add_executable(
    server
    main.cpp
    database.cpp
    setup_cli_options.cpp
    commands/command_type.cpp
    commands/crud.cpp
    commands/exists.cpp
    commands/handler.cpp
    commands/ping.cpp
    commands/ttl.cpp
    net/session.cpp
    net/tcp_server.cpp)

target_compile_definitions(server PUBLIC BOOST_ASIO_NO_DEPRECATED)

target_link_libraries(server PRIVATE redis::core CLI11::CLI11)
if(boost_asio_FOUND)
    target_link_libraries(server PUBLIC Boost::asio)
else()
    target_link_libraries(server PUBLIC Boost::thread Boost::system)
endif()
add_executable(redis::server ALIAS server)

