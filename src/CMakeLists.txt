add_library(
	redis_core SHARED
	core/error/simple_message.cpp
	core/logging/logger.cpp
	core/resp/parser.cpp
	core/resp/value.cpp
	core/utils/create_temp_file.cpp
	core/utils/deadline_timer.cpp
	core/utils/toupper.cpp)

add_library(redis::core ALIAS redis_core)

target_compile_options(redis_core PUBLIC -fsanitize=address)
target_link_options(redis_core PUBLIC -fsanitize=address)
target_link_libraries(redis_core PUBLIC spdlog::spdlog magic_enum::magic_enum
	FastFloat::fast_float)

target_include_directories(
	redis_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INCLUDE_DIR}>)

add_executable(
	redis_server
	server/commands/command_type.cpp
	server/commands/crud.cpp
	server/commands/exists.cpp
	server/commands/handler.cpp
	server/commands/ping.cpp
	server/commands/ttl.cpp
	server/memory/database.cpp
	server/net/redis_server.cpp
	server/net/session.cpp
	server/net/tcp_server.cpp
	server/main.cpp
	server/setup_cli_options.cpp)

target_compile_definitions(redis_server PUBLIC BOOST_ASIO_NO_DEPRECATED)

target_link_libraries(redis_server PRIVATE redis::core CLI11::CLI11)
if (boost_asio_FOUND)
	target_link_libraries(redis_server PUBLIC Boost::asio)
else ()
	target_link_libraries(redis_server PUBLIC Boost::thread Boost::system)
endif ()

add_executable(redis::server ALIAS redis_server)

# --- Installation ---
install(
    TARGETS redis_core redis_server
    EXPORT redisTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include)

# --- Export Targets ---
install(
    EXPORT redisTargets
    FILE redisTargets.cmake
    NAMESPACE redis::
    DESTINATION lib/cmake/redis)
