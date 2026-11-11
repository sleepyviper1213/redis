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
    unit/logging.setup.cpp

    unit/core/chrono/date_time.test.cpp
    unit/core/chrono/deadline_timer.test.cpp
    unit/core/stdexec_ext/retry.test.cpp
    unit/common/store/simple_dynamic_string.test.cpp
    unit/core/utils/glob.test.cpp
    unit/core/utils/indirect.test.cpp
    unit/core/utils/parse_number.test.cpp
    unit/core/utils/ring_buffer.test.cpp
    unit/core/utils/saturating.test.cpp

    unit/common/resp/format_spec.test.cpp
    unit/common/resp/parser.test.cpp
    unit/common/resp/request.test.cpp
    unit/common/store/allocator.test.cpp
    unit/common/store/database.test.cpp
    unit/server/persistence/aof.test.cpp
    unit/server/commands/config.test.cpp
    unit/server/commands/handler.test.cpp
    unit/server/commands/hash.test.cpp
    unit/server/commands/info.test.cpp
    unit/server/commands/keyspace.test.cpp
    unit/server/commands/list.test.cpp
    unit/server/commands/reply.test.cpp
    unit/server/commands/set.test.cpp
    unit/server/commands/set_ttl.test.cpp
    unit/server/commands/string.test.cpp
    unit/server/net/executor.test.cpp
    unit/server/net/guard.test.cpp
    unit/server/net/net_error.test.cpp
    unit/server/net/net_error_debug.test.cpp
    unit/server/net/pipeline.test.cpp
    unit/server/net/v2_session.test.cpp
    unit/server/persistence/persist_error.test.cpp
    unit/server/persistence/persist_error_debug.test.cpp
    unit/core/os/save_file.test.cpp
    unit/server/persistence/snapshot.test.cpp
)
target_link_libraries(redis_unit_test PRIVATE Catch2::Catch2WithMain
                                              redis::server_lib)
target_include_directories(redis_unit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_warnings(redis_unit_test)
enable_coverage(redis_unit_test)
copy_sanitizer_runtime(redis_unit_test)
if(WIN32)
    # Windows cannot fork, so BGSAVE is unavailable there.
    catch_discover_tests(redis_unit_test TEST_SPEC "~[fork]")
else()
    catch_discover_tests(redis_unit_test)
endif()

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
                                                     Hiredis::Hiredis fmt::fmt)
if(WIN32)
    # A static hiredis does not carry its Windows sockets dependency.
    target_link_libraries(redis_integration_test PRIVATE ws2_32)
endif()
copy_sanitizer_runtime(redis_integration_test)

# Stress test: many clients checking every reply against a model, plus
# fragmented and malformed input. Run via scripts/stress_server.sh, or directly
# against a running server. Unlike redis-benchmark it verifies the replies, and
# on a failure names the client, the request and the first wrong byte.
add_executable(redis_stress_test stress/stress.cpp)
target_link_libraries(redis_stress_test PRIVATE Hiredis::Hiredis CLI11::CLI11
                                               Threads::Threads fmt::fmt)
if(WIN32)
    target_link_libraries(redis_stress_test PRIVATE ws2_32)
endif()
if(boost_asio_FOUND)
    target_link_libraries(redis_stress_test PRIVATE Boost::asio)
else()
    target_link_libraries(redis_stress_test PRIVATE Boost::thread Boost::system)
endif()
target_compile_definitions(redis_stress_test PRIVATE BOOST_ASIO_NO_DEPRECATED)
set_warnings(redis_stress_test)
copy_sanitizer_runtime(redis_stress_test)
