# Overlay of vcpkg's community x64-mingw-static triplet, for the windows-mingw
# preset (VCPKG_OVERLAY_TRIPLETS points at this directory).
#
# reflect-cpp's sources put more sections in one object than PE/COFF allows by
# default, and GNU as fails with "file too big"; -mbig-obj lifts the limit.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PATH)

set(VCPKG_CMAKE_SYSTEM_NAME MinGW)

if(PORT STREQUAL "reflectcpp")
    set(VCPKG_C_FLAGS "")  # vcpkg wants both or neither
    set(VCPKG_CXX_FLAGS "-Wa,-mbig-obj")
endif()
