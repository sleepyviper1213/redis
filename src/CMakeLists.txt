add_executable(
  redis_server
  actions/db.cpp
  actions/snapshot.cpp
  actions/gate.cpp
  commands/cmd.cpp
  commands/cmd.cpp
  commands/command_type.cpp
  error/error.cpp
  net/http_server.cpp
  net/http_resource.cpp
  main.cpp
  primitive/value.cpp
  utils/split_by_space.cpp)

target_include_directories(redis_server PUBLIC .)
target_link_libraries(redis_server PUBLIC spdlog::spdlog Boost::system
                                          Boost::thread cereal::cereal)
target_compile_definitions(redis_server PUBLIC BOOST_ASIO_NO_DEPRECATED)