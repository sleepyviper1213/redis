# Homebrew LLVM toolchain for Apple Silicon.
#
# Pins the C/C++ compiler to Homebrew's LLVM and links against *its* libc++ /
# libunwind rather than AppleClang's, so C++23 library features (std::expected,
# ranges) and the standard-library ABI are the same across the project and its
# vcpkg dependencies. AppleClang's libc++ lags upstream LLVM.
#
# This file is chainloaded by vcpkg (VCPKG_CHAINLOAD_TOOLCHAIN_FILE), so both the
# ports and the project itself build with the same compiler + stdlib.
#
# Override the install prefix if Homebrew is elsewhere or you pin a version:
#   -DHOMEBREW_LLVM_PREFIX=/opt/homebrew/opt/llvm@19
# or export HOMEBREW_LLVM_PREFIX before configuring.

if(NOT HOMEBREW_LLVM_PREFIX)
    if(DEFINED ENV{HOMEBREW_LLVM_PREFIX})
        set(HOMEBREW_LLVM_PREFIX "$ENV{HOMEBREW_LLVM_PREFIX}")
    else()
        set(HOMEBREW_LLVM_PREFIX "/opt/homebrew/opt/llvm")
    endif()
endif()

if(NOT EXISTS "${HOMEBREW_LLVM_PREFIX}/bin/clang++")
    message(FATAL_ERROR
        "Homebrew LLVM not found at '${HOMEBREW_LLVM_PREFIX}'. "
        "Install it with `brew install llvm`, or pass "
        "-DHOMEBREW_LLVM_PREFIX=<prefix> (see `brew --prefix llvm`).")
endif()

set(CMAKE_C_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang" CACHE FILEPATH "Homebrew LLVM clang" FORCE)
set(CMAKE_CXX_COMPILER "${HOMEBREW_LLVM_PREFIX}/bin/clang++" CACHE FILEPATH "Homebrew LLVM clang++" FORCE)

# Use the LLVM toolchain's own libc++ / libunwind and bake an rpath so binaries
# find them at run time. Mirrors the `brew info llvm` caveats.
#
# libunwind too, though libSystem carries an unwinder of its own: Homebrew's
# libc++abi links @rpath/libunwind.1.dylib, so exceptions are unwound by it
# whatever this file says. Linking it here binds the binary's own _Unwind_*
# calls to the same copy. The one place the two still meet is
# __gcc_personality_v0, which only libSystem provides; see
# cmake/sanitizers/Thread.cmake for the one user of it.
set(_hb_llvm_link
    "-L${HOMEBREW_LLVM_PREFIX}/lib/c++ -Wl,-rpath,${HOMEBREW_LLVM_PREFIX}/lib/c++ \
-L${HOMEBREW_LLVM_PREFIX}/lib/unwind -lunwind")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_hb_llvm_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_hb_llvm_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_hb_llvm_link}")
unset(_hb_llvm_link)
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)

# Chainloading replaces vcpkg's own macOS toolchain, which is what applies the
# triplet's VCPKG_C_FLAGS / VCPKG_CXX_FLAGS to ports. Apply them here (vcpkg
# passes them as -D only when building a port; the project itself has none).
if(VCPKG_C_FLAGS)
    string(APPEND CMAKE_C_FLAGS_INIT " ${VCPKG_C_FLAGS}")
endif()
if(VCPKG_CXX_FLAGS)
    string(APPEND CMAKE_CXX_FLAGS_INIT " ${VCPKG_CXX_FLAGS}")
endif()
