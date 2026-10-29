include_guard(GLOBAL)


set(REDIS_SANITIZER_CONFIGS "")

function(copy_sanitizer_runtime target)
    # Stub. sanitizers/Address.cmake replaces this with the MSVC DLL staging
    # command when AddressSanitizer is registered.
endfunction()

function(_redis_add_sanitizer_flags config)
    set(_cond "$<CONFIG:${config}>")
    set(_opts)
    foreach(_f IN LISTS ARGN)
        list(APPEND _opts "$<${_cond}:${_f}>")
    endforeach()
    add_compile_options(${_opts})
    add_link_options(${_opts})
endfunction()

function(_redis_sanitizer_uninstrumented config reason)
    message(STATUS "${reason}; '${config}' builds WITHOUT instrumentation.")
endfunction()

if(NOT REDIS_SANITIZER)
    set(REDIS_SANITIZER_CONFIGS
        ""
        CACHE INTERNAL "Registered sanitizer configuration type names" FORCE)
    message(
        STATUS
            "sanitizers: configs not registered (REDIS_SANITIZER=OFF)")
    return()
endif()
# ---------------------------------------------------------------------------
# Register <Name> as a RelWithDebInfo-based configuration type
# ---------------------------------------------------------------------------
macro(_redis_register_sanitizer_config config_name)
    string(TOUPPER "${config_name}" _rd_san_upper)

    if(CMAKE_CONFIGURATION_TYPES
       AND NOT REDIS_ENABLE_COVERAGE
       AND NOT "${config_name}" IN_LIST CMAKE_CONFIGURATION_TYPES)
        list(APPEND CMAKE_CONFIGURATION_TYPES "${config_name}")
        set(CMAKE_CONFIGURATION_TYPES
            "${CMAKE_CONFIGURATION_TYPES}"
            CACHE STRING "Supported configuration types" FORCE)
    endif()

    foreach(_lang C CXX)
        set(CMAKE_${_lang}_FLAGS_${_rd_san_upper}
            "${CMAKE_${_lang}_FLAGS_RELWITHDEBINFO}"
            CACHE
                STRING
                "Flags used by the ${_lang} compiler for the ${config_name} build type."
                FORCE)
        mark_as_advanced(CMAKE_${_lang}_FLAGS_${_rd_san_upper})
    endforeach()

    foreach(_type EXE SHARED MODULE STATIC)
        set(CMAKE_${_type}_LINKER_FLAGS_${_rd_san_upper}
            "${CMAKE_${_type}_LINKER_FLAGS_RELWITHDEBINFO}"
            CACHE
                STRING
                "Linker flags for ${_type} targets in the ${config_name} build type."
                FORCE)
        mark_as_advanced(CMAKE_${_type}_LINKER_FLAGS_${_rd_san_upper})
    endforeach()

    set(CMAKE_MAP_IMPORTED_CONFIG_${_rd_san_upper} Release RelWithDebInfo "")
    list(APPEND REDIS_SANITIZER_CONFIGS "${config_name}")
    unset(_rd_san_upper)
endmacro()

# ---------------------------------------------------------------------------
# Families: each one registers its own configuration type, and its flags apply
# only to that configuration, so several families can share one build tree.
# ---------------------------------------------------------------------------
function(_redis_normalize_sanitizer_token token out_var)
    string(TOUPPER "${token}" _u)
    string(REPLACE "-" "" _u "${_u}")
    string(REPLACE "_" "" _u "${_u}")
    string(REPLACE "SANITIZER" "" _u "${_u}")
    if(_u STREQUAL "ADDRESS" OR _u STREQUAL "ASAN")
        set(${out_var}
            Address
            PARENT_SCOPE)
    elseif(_u STREQUAL "THREAD" OR _u STREQUAL "TSAN")
        set(${out_var}
            Thread
            PARENT_SCOPE)
    elseif(_u STREQUAL "UNDEFINED" OR _u STREQUAL "UBSAN")
        set(${out_var}
            Undefined
            PARENT_SCOPE)
    else()
        set(${out_var}
            ""
            PARENT_SCOPE)
    endif()
endfunction()

string(REPLACE "," ";" _rd_san_tokens "${REDIS_SANITIZER}")
set(_rd_san_families "")
foreach(_rd_san_token IN LISTS _rd_san_tokens)
    _redis_normalize_sanitizer_token("${_rd_san_token}" _rd_san_family)
    if(NOT _rd_san_family)
        message(
            FATAL_ERROR
                "REDIS_SANITIZER='${REDIS_SANITIZER}' has '${_rd_san_token}', "
                "which is not one of: Address, Thread, Undefined.")
    endif()
    list(APPEND _rd_san_families "${_rd_san_family}")
endforeach()
list(REMOVE_DUPLICATES _rd_san_families)

foreach(_rd_san_family IN LISTS _rd_san_families)
    include("${CMAKE_CURRENT_LIST_DIR}/sanitizers/${_rd_san_family}.cmake")
endforeach()
unset(_rd_san_tokens)
unset(_rd_san_token)
unset(_rd_san_families)
unset(_rd_san_family)

set(REDIS_SANITIZER_CONFIGS
    "${REDIS_SANITIZER_CONFIGS}"
    CACHE INTERNAL "Registered sanitizer configuration type names" FORCE)

if(CMAKE_BUILD_TYPE)
    set(_rd_type_strings Debug Release RelWithDebInfo MinSizeRel)
    list(APPEND _rd_type_strings ${REDIS_SANITIZER_CONFIGS})
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${_rd_type_strings})
    unset(_rd_type_strings)
endif()


list(GET REDIS_SANITIZER_CONFIGS 0 _rd_san_cfg)
message(STATUS "sanitizers: extra build type: ${_rd_san_cfg}")
if(NOT CMAKE_CONFIGURATION_TYPES)
    message(
        STATUS
            "sanitizers: single-config — cmake -DCMAKE_BUILD_TYPE=${_rd_san_cfg}"
    )
else()
    message(
        STATUS
            "sanitizers: multi-config  — cmake --build --config ${_rd_san_cfg}")
endif()
unset(_rd_san_cfg)
