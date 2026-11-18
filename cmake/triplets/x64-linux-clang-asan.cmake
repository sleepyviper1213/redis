# Custom vcpkg triplet: x64 Linux, every port built with clang under
# AddressSanitizer. Selected by the x86_64-linux-gnu-clang-asan preset.
#
# Abseil's hash tables poison their empty slots and keep a generation counter
# when compiled under ASan; the library and the headers our code includes must
# agree on that, which an uninstrumented library does not. A shared tree cannot
# instrument the dependencies for one configuration only, hence the tree of its
# own.
include("${CMAKE_CURRENT_LIST_DIR}/detail/x64-linux.cmake")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/x86_64-linux-gnu-clang.cmake")

set(VCPKG_C_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_LINKER_FLAGS "-fsanitize=address")
