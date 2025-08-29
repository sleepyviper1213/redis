add_executable(redis_unit_test unit/resp/parser.cpp)
target_link_libraries(redis_unit_test PUBLIC Catch2::Catch2WithMain redis::core)

add_executable(redis_server_test server/ping_pong.cpp
	server/wait_for_port.hpp)
target_link_libraries(redis_server_test PRIVATE Catch2::Catch2WithMain hiredis)

# Pass server binary path into test code
target_compile_definitions(redis_server_test
	PRIVATE REDIS_SERVER_PATH="$<TARGET_FILE:redis::server>")
if(REDIS_SERVER_LOGS)
    target_compile_definitions(redis_server_test PRIVATE REDIS_SERVER_LOGS)
endif ()


include(Catch)
catch_discover_tests(redis_server_test)
