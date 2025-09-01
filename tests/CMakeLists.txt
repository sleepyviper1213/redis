add_executable(redis_unit_test unit/resp/parser.cpp)
target_link_libraries(redis_unit_test PUBLIC Catch2::Catch2WithMain redis::core)

add_executable(redis_server_test
	server/del.cpp
	server/get.cpp
	server/ping_pong.cpp
	server/save.cpp
	server/set.cpp
)
target_link_libraries(redis_server_test PRIVATE Catch2::Catch2WithMain hiredis)