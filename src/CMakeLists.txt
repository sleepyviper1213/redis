add_executable(
  redis_server
  actions/db.cpp
  actions/snapshot.cpp
  actions/gate.cpp
  commands/cmd.cpp
  commands/keyspace_command.cpp
  commands/list_command.cpp
  error/parse_error.cpp
  error/snapshot_error.cpp
  error/ttl_error.cpp
  net/http_server.cpp
  net/http_resource.cpp
  main.cpp
  primitive/value.cpp
  utils/split_by_space.cpp)

target_include_directories(redis_server PUBLIC .)
target_link_libraries(redis_server PUBLIC spdlog::spdlog Boost::system
                                          Boost::thread cereal::cereal)
