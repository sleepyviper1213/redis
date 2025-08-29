find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
find_library(HIREDIS_LIBRARY hiredis)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Hiredis DEFAULT_MSG HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)

if(HIREDIS_FOUND)
    add_library(Hiredis::Hiredis UNKNOWN IMPORTED)
    set_target_properties(Hiredis::Hiredis PROPERTIES
        IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}")
endif()

