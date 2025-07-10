add_executable(
    redis_server
    commands/cmd.cpp
    commands/command_type.cpp
    commands/keyspace_command.cpp
    commands/list_command.cpp
    db.cpp
    error/app_error.cpp
    gate.cpp
    http_server.cpp
    http_resource.cpp
    main.cpp
    value.cpp
    snapshot.cpp
    utility.cpp)
target_include_directories(redis_server PUBLIC .)
target_link_libraries(redis_server PUBLIC spdlog::spdlog Boost::system cereal::cereal)
