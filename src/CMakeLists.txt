add_library(
    core
    core/error/simple_message.cpp
    core/resp/parser.cpp
    core/resp/value.cpp
    core/utils/get_class_logger.cpp
    core/utils/deadline_timer.cpp
    core/utils/create_temp_file.cpp
    core/utils/toupper.cpp)

add_library(redis_server::core ALIAS core)

target_compile_options(core PUBLIC -fsanitize=address)
target_link_options(core PUBLIC -fsanitize=address)
target_link_libraries(core PUBLIC spdlog::spdlog magic_enum::magic_enum
                                  FastFloat::fast_float)

target_include_directories(
    core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
                $<INSTALL_INTERFACE:include>)


add_executable(
    redis_server
    server/main.cpp
    server/database.cpp
    server/setup_cli_options.cpp
    server/commands/command_type.cpp
    server/commands/crud.cpp
    server/commands/exists.cpp
    server/commands/handler.cpp
    server/commands/ping.cpp
    server/commands/ttl.cpp
    server/net/session.cpp
    server/net/tcp_server.cpp)
target_compile_definitions(redis_server PUBLIC BOOST_ASIO_NO_DEPRECATED)

target_link_libraries(redis_server PRIVATE redis_server::core CLI11::CLI11)
if(boost_asio_FOUND)
    target_link_libraries(redis_server PUBLIC Boost::asio)
else()
    target_link_libraries(redis_server PUBLIC Boost::thread Boost::system)
endif()
add_executable(redis_server::redis_server ALIAS redis_server)

# --- Installation ---
install(
    TARGETS core redis_server
    EXPORT redis_serverTargets
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES
    DESTINATION include)

install(DIRECTORY include/ DESTINATION include)

# --- Export Targets ---
install(
    EXPORT redis_serverTargets
    FILE redis_serverTargets.cmake
    NAMESPACE redis::
    DESTINATION lib/cmake/redis_server)
