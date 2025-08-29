add_subdirectory(core)
add_subdirectory(server)

# --- Installation ---
install(
    TARGETS core server
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
