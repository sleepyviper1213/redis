# Custom vcpkg triplet: x64 Linux with every port built under AddressSanitizer.
#
# Otherwise as the built-in x64-linux. Selected by the linux-*-asan presets
# (asan-deps-linux). Abseil's hash tables poison their empty slots and keep a
# generation counter when compiled under ASan; the library and the headers our
# code includes must agree on that, which an uninstrumented library does not.
# A shared tree cannot instrument the dependencies for one configuration only,
# hence the tree of its own.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_C_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_LINKER_FLAGS "-fsanitize=address")
