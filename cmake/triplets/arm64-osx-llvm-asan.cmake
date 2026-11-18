# Custom vcpkg triplet: arm64-osx-llvm with every port built under
# AddressSanitizer. See x64-linux-gcc-asan.cmake for why. Selected by the
# aarch64-apple-darwin-llvm-asan preset (asan-deps-macos).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/homebrew-llvm.cmake")

# As arm64-osx-llvm: stdexec's libdispatch_queue.hpp needs <vector>.
set(VCPKG_C_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_CXX_FLAGS "-include vector -fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_LINKER_FLAGS "-fsanitize=address")
