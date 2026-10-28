include_guard(GLOBAL)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(
    TARGETS redis_core redis_server
    EXPORT redisTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Headers live next to their sources; install only the core library's public
# headers, keeping their include paths ("core/...").
install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/src/core
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "*.hpp")
install(FILES ${PROJECT_SOURCE_DIR}/src/core.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
    EXPORT redisTargets
    FILE redisTargets.cmake
    NAMESPACE redis::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/redis)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/redisConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/PackageConfigHelpers.cmake.in"
    "${PROJECT_BINARY_DIR}/redisConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/redis)

install(FILES "${PROJECT_BINARY_DIR}/redisConfig.cmake"
              "${PROJECT_BINARY_DIR}/redisConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/redis)
