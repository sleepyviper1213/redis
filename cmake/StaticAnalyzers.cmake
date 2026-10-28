include_guard(GLOBAL)

if (REDIS_ENABLE_STATIC_ANALYZERS) 
	set(REDIS_ENABLE_CLANG_TIDY ON)
	set(REDIS_ENABLE_CPPCHECK ON)
endif()

if(REDIS_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY clang-tidy)

    if(CLANG_TIDY)
        # clang-tidy re-parses the compiler's own command line. On a GCC build
        # that line carries flags clang rejects: unknown -W..., and -f... it
        # accepts but does not use, which -Werror then promotes.
        set(CMAKE_CXX_CLANG_TIDY
            "${CLANG_TIDY}"
            "--extra-arg=-Wno-unknown-warning-option"
            "--extra-arg=-Wno-unused-command-line-argument")

        # An unknown -f... is a driver error instead, which no -Wno- suppresses,
        # so the flag itself has to go. CMake spells
        # CMAKE_VISIBILITY_INLINES_HIDDEN as -fno-keep-inline-dllexport on
        # Windows-GNU; dropping it costs a larger object in a tree that is
        # analysed and measured, never shipped.
        if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN "")

            # clang defaults to the MSVC triple on Windows, so without this
            # clang-tidy analyses a MinGW build against MSVC's STL and reports
            # on headers the compiler never reads.
            list(APPEND CMAKE_CXX_CLANG_TIDY
                 "--extra-arg=--target=x86_64-w64-windows-gnu")
        endif()

        # GCC enables sized deallocation in C++14 and later; clang does not, so
        # without this clang-tidy cannot see
        # operator delete(void*, size_t, align_val_t) and allocator-aware code fails
        # to parse.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND CMAKE_CXX_CLANG_TIDY
                 "--extra-arg=-fsized-deallocation")
        endif()
    endif()

endif()

if(REDIS_ENABLE_CPPCHECK)

    find_program(CPPCHECK cppcheck)

    if(CPPCHECK)
        # cppcheck has no coroutine support: co_return does not read as a
        # return, so every awaitable looks like a path that falls off the end.
        # Not scoped to a path: the network layer is all coroutines. Nothing is lost: falling off the end of a
        # non-void function is already a compiler diagnostic.
        set(CMAKE_CXX_CPPCHECK
            "${CPPCHECK}"
            "--inline-suppr"
            "--suppress=missingReturn"
            "--suppress=*:*third_party*")
    endif()

endif()
