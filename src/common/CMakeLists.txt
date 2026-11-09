# What Redis speaks and stores, shared by the server and anything else that
# talks to it (a client, a tool, the tests): the RESP codec and the typed
# keyspace. Knows about Redis, knows nothing of sockets, commands or files.
# Depends on core only.
add_library(
    redis_common
    resp/parser.cpp
    resp/request.cpp
    resp/value.cpp
)

add_library(redis::common ALIAS redis_common)

target_include_directories(
    redis_common PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
                        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(redis_common PUBLIC redis::core FastFloat::fast_float)

set_warnings(redis_common)
enable_coverage(redis_common)
