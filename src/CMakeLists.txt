add_library(
	redis_core SHARED
	core/error/simple_message.cpp
	core/logger.cpp
	resp/parser.cpp
	resp/value.cpp
	utils/create_temp_file.cpp
	utils/deadline_timer.cpp
	utils/toupper.cpp
	commands/command_type.cpp
	commands/crud.cpp
	commands/exists.cpp
	commands/list.cpp
	commands/handler.cpp
	commands/ping.cpp
	commands/ttl.cpp
	memory/database.cpp
	server/net/redis_server.cpp
	server/net/session.cpp
	server/net/tcp_server.cpp)

add_library(redis::core ALIAS redis_core)

target_compile_options(redis_core PUBLIC
	$<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:
	-Wall -Wextra -pedantic
	>
	$<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(REDIS_ASAN_ENABLED)
	target_compile_options(redis_core PRIVATE -fsanitize=address -fno-omit-frame-pointer -O1)
	target_link_options(redis_core PRIVATE -fsanitize=address)
endif()

target_link_libraries(
	redis_core PUBLIC spdlog::spdlog magic_enum::magic_enum
	FastFloat::fast_float)

target_include_directories(
	redis_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
	$<INSTALL_INTERFACE:${CMAKE_INCLUDE_DIR}>)

if(boost_asio_FOUND)
	target_link_libraries(redis_core PUBLIC Boost::asio)
else()
	target_link_libraries(redis_core PUBLIC Boost::thread)
endif()

add_executable(redis_server cli/main.cpp cli/setup_cli_options.cpp)

target_compile_definitions(redis_server PUBLIC BOOST_ASIO_NO_DEPRECATED)

target_link_libraries(redis_server PRIVATE redis::core CLI11::CLI11)

add_executable(redis::server ALIAS redis_server)

# --- Installation ---
# install( TARGETS redis_core redis_server EXPORT redisTargets LIBRARY
# DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin INCLUDES
# DESTINATION include) install(DIRECTORY ${PROJECT_SOURCE_DIR}/src DESTINATION
# include)
#
# --- Export Targets ---
# install( EXPORT redisTargets FILE redisTargets.cmake NAMESPACE redis::
# DESTINATION lib/cmake/redis)
