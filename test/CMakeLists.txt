# The test tree mirrors src/: a failure's path says which module and component
# it came from before you open anything.
#
#     <module>/<group>/<component>.test.cpp
#
# Helpers shared by sibling suites live in a `<component>.fixture.hpp` next to
# them rather than being copied.

include(Catch)

# Unit tests: in-process, no network, run by CTest.
add_executable(redis_unit_test
    logging.setup.cpp

    core/resp/parser.test.cpp
    core/resp/request.test.cpp
    core/utils/glob.test.cpp

    server/commands/hash.test.cpp
    server/commands/keyspace.test.cpp
    server/commands/list.test.cpp
    server/commands/set.test.cpp
    server/commands/set_ttl.test.cpp
    server/commands/string.test.cpp
    server/net/pipeline.test.cpp
    server/persistence/snapshot.test.cpp
    server/storage/database.test.cpp
)
target_link_libraries(redis_unit_test PRIVATE Catch2::Catch2WithMain
                                              redis::server_lib)
set_warnings(redis_unit_test)
enable_coverage(redis_unit_test)
copy_sanitizer_runtime(redis_unit_test)
catch_discover_tests(redis_unit_test)

# Integration tests: drive a running redis_server through hiredis, the C client
# real applications use. Run via scripts/test_server.sh, which starts the server.
add_executable(redis_integration_test
    integration/del.test.cpp
    integration/get.test.cpp
    integration/ping_pong.test.cpp
    integration/save.test.cpp
    integration/set.test.cpp
)
target_link_libraries(redis_integration_test PRIVATE Catch2::Catch2WithMain
                                                     Hiredis::Hiredis)
copy_sanitizer_runtime(redis_integration_test)
