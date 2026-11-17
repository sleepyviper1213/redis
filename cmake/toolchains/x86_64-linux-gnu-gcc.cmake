# GCC toolchain for x86_64 Linux.
#
# Pins the C/C++ compiler to GCC. Chainloaded by vcpkg
# (VCPKG_CHAINLOAD_TOOLCHAIN_FILE), both from the preset, for the project, and
# from the x64-linux-gcc* triplets, for the ports, so the two build with the
# same compiler whatever CC and CXX say.
set(CMAKE_C_COMPILER gcc CACHE FILEPATH "GCC" FORCE)
set(CMAKE_CXX_COMPILER g++ CACHE FILEPATH "G++" FORCE)
