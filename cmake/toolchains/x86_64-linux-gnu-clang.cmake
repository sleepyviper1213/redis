# Clang toolchain for x86_64 Linux.
#
# Pins the C/C++ compiler to Clang. Chainloaded by vcpkg
# (VCPKG_CHAINLOAD_TOOLCHAIN_FILE), both from the preset, for the project, and
# from the x64-linux-clang* triplets, for the ports, so the two build with the
# same compiler whatever CC and CXX say.
set(CMAKE_C_COMPILER clang CACHE FILEPATH "Clang" FORCE)
set(CMAKE_CXX_COMPILER clang++ CACHE FILEPATH "Clang++" FORCE)

include("${CMAKE_CURRENT_LIST_DIR}/detail/vcpkg-port-flags.cmake")
