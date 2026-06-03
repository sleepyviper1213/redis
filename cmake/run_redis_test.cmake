# cmake/run_redis_test.cmake

# Launch redis_server in background
execute_process(
    COMMAND ${REDIS_SERVER_EXE}
    OUTPUT_FILE redis_server.log
    ERROR_FILE redis_server.err
    TIMEOUT 1
    RESULT_VARIABLE SERVER_RESULT
    # important: don't block here
)

# Wait a bit for the server to bind
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 1)

# Run the test executable
execute_process(
    COMMAND ${TEST_REDIS_EXE}
    RESULT_VARIABLE TEST_RESULT
)

# Kill the server (on Unix/macOS)
execute_process(COMMAND pkill -f ${REDIS_SERVER_EXE})

# Fail test if Catch2 failed
if (NOT TEST_RESULT EQUAL 0)
    message(FATAL_ERROR "Catch2 tests failed with code ${TEST_RESULT}")
endif()

