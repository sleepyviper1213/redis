include_guard(GLOBAL)


# The environment can set it too: a workflow preset takes no -D flag, which is
# how CI switches ccache (REDIS_ENABLE_CCACHE=OFF in the workflow's env). It wins
# over the preset's value.
if(DEFINED ENV{REDIS_ENABLE_CCACHE})
    set(REDIS_ENABLE_CCACHE "$ENV{REDIS_ENABLE_CCACHE}")
endif()

if(NOT REDIS_ENABLE_CCACHE)
    message(STATUS "ccache: disabled (REDIS_ENABLE_CCACHE=OFF)")
    return()
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    message(STATUS
        "ccache: found (${CCACHE_PROGRAM}) but the ${CMAKE_GENERATOR} "
        "generator never runs a compiler launcher — NOT enabled. Configure "
        "with a Ninja-based preset to get a compiler cache.")
elseif(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache: enabled (${CCACHE_PROGRAM})")
else()
    message(WARNING
        "ccache: NOT found on PATH — building WITHOUT a compiler cache. "
        "Install ccache and make sure it is on the PATH the configure process "
        "sees (restart the IDE after changing PATH), then re-configure.")
endif()
