# What vcpkg's own Linux toolchain does with the triplet's flags, for a toolchain
# that replaces it.
#
# vcpkg reads VCPKG_C_FLAGS, VCPKG_CXX_FLAGS and VCPKG_LINKER_FLAGS (and the
# _DEBUG/_RELEASE kinds) in scripts/toolchains/linux.cmake, which it leaves out
# whenever a triplet chainloads a toolchain of its own: without this, an -asan
# triplet builds every port uninstrumented, with no -fPIC either. They are
# defined only while a port is built (vcpkg passes them as -D), so the project's
# own build, which also loads the toolchain, is left alone.
if(DEFINED VCPKG_C_FLAGS OR DEFINED VCPKG_CXX_FLAGS)
    foreach(_lang C CXX)
        string(APPEND CMAKE_${_lang}_FLAGS_INIT " -fPIC ${VCPKG_${_lang}_FLAGS} ")
        foreach(_kind DEBUG RELEASE)
            string(APPEND CMAKE_${_lang}_FLAGS_${_kind}_INIT
                   " ${VCPKG_${_lang}_FLAGS_${_kind}} ")
        endforeach()
    endforeach()
    foreach(_target EXE SHARED MODULE)
        string(APPEND CMAKE_${_target}_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS} ")
        foreach(_kind DEBUG RELEASE)
            string(APPEND CMAKE_${_target}_LINKER_FLAGS_${_kind}_INIT
                   " ${VCPKG_LINKER_FLAGS_${_kind}} ")
        endforeach()
    endforeach()
    unset(_lang)
    unset(_kind)
    unset(_target)
endif()
