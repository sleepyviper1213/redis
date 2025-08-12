add_executable(
    redis_unit_test EXCLUDE_FROM_ALL
	resp/parser.cpp
    test.cpp)

target_link_libraries(
    redis_unit_test PUBLIC
    Catch2::Catch2WithMain redis_server_lib)
