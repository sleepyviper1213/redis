# What every x64-linux-* triplet here shares: the built-in x64-linux, which the
# triplets then give a compiler and, for the -asan ones, instrumentation.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
