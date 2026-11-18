# Custom vcpkg triplet: x64 Linux, every port built with clang.
# Selected by the x86_64-linux-gnu-clang presets.
include("${CMAKE_CURRENT_LIST_DIR}/detail/x64-linux.cmake")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/x86_64-linux-gnu-clang.cmake")
