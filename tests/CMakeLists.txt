add_executable(
    redis_unit_test EXCLUDE_FROM_ALL
	unit/resp/parser.cpp
)
target_link_libraries(
    redis_unit_test PUBLIC
    Catch2::Catch2WithMain core)
