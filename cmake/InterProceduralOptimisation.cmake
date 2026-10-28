include_guard(GLOBAL)

if(NOT REDIS_ENABLE_IPO)
    message(STATUS "IPO/LTO: off (REDIS_ENABLE_IPO=OFF)")
    return()
endif()

set(_rd_link_default "${CMAKE_LINKER_TYPE}")
set(_rd_link_lto "${CMAKE_LINKER_TYPE}")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_LINKER_TYPE STREQUAL "LLD")
    set(_rd_link_lto BFD)
endif()

set(CMAKE_LINKER_TYPE "${_rd_link_lto}")
include(CheckIPOSupported)
check_ipo_supported(RESULT _rd_has_ipo OUTPUT _rd_ipo_log LANGUAGES CXX)
set(CMAKE_LINKER_TYPE "${_rd_link_default}")

if(NOT _rd_has_ipo)
    message(WARNING
            "IPO/LTO: ${CMAKE_CXX_COMPILER_ID} cannot link with it here — "
            "building without. ${_rd_ipo_log}")
    return()
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

if(_rd_link_lto STREQUAL _rd_link_default)
    message(STATUS "IPO/LTO: on for Release and RelWithDebInfo")
else()
    set(CMAKE_LINKER_TYPE
        "$<IF:$<CONFIG:Release,RelWithDebInfo>,${_rd_link_lto},${_rd_link_default}>")
    message(STATUS
            "IPO/LTO: on for Release and RelWithDebInfo, which link with "
            "${_rd_link_lto} — ${_rd_link_default} cannot read this compiler's "
            "LTO objects. Other configurations keep ${_rd_link_default}.")
endif()

unset(_rd_link_default)
unset(_rd_link_lto)
unset(_rd_has_ipo)
unset(_rd_ipo_log)
