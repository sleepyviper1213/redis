# Custom vcpkg triplet: arm64 macOS built with Homebrew LLVM.
#
# Same as the built-in arm64-osx triplet, but chainloads the Homebrew LLVM
# toolchain so vcpkg compiles every port with LLVM's Clang + libc++, matching
# the compiler used for the project itself. Selected by the macos-arm64-llvm
# presets (VCPKG_OVERLAY_TRIPLETS points at this directory).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/homebrew-llvm.cmake")
