# Protocol and infrastructure with no networking: the RESP codec, error
# handling, logging and time utilities.
add_library(
    redis_core
    error/simple_message.cpp
    logging/channels.cpp
    logging/guard.cpp
    logging/lifecycle.cpp
    resp/parser.cpp
    resp/request.cpp
    resp/value.cpp
    utils/glob.cpp
    utils/toupper.cpp)

add_library(redis::core ALIAS redis_core)

target_include_directories(
    redis_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
                      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(redis_core PUBLIC spdlog::spdlog magic_enum::magic_enum
                                        FastFloat::fast_float)

set_warnings(redis_core)
enable_coverage(redis_core)
