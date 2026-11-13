include_guard(GLOBAL)

# ThreadSanitizer build configuration (mirrors RelWithDebInfo + TSan). Separate
# from AddressSanitizer because ASan and TSan cannot coexist in one binary. TSan's
# runtime exists only for Clang/AppleClang/GCC on macOS/Linux; MSVC and MinGW have
# none, so the config is uninstrumented on Windows and only the macOS and Linux
# toolchains get *-tsan presets.

_redis_register_sanitizer_config(ThreadSanitizer)

if(WIN32)
    _redis_sanitizer_uninstrumented(ThreadSanitizer
        "Windows has no ThreadSanitizer runtime for MSVC or MinGW")
else()
    _redis_add_sanitizer_flags(ThreadSanitizer
        -fsanitize=thread -fno-omit-frame-pointer -g)
    # TSan gives every instrumented function an unwind cleanup (to pop its
    # shadow stack frame); a function with no personality of its own gets
    # __gcc_personality_v0. With Homebrew LLVM on macOS only libSystem provides
    # that, and it calls libSystem's _Unwind_GetIP on a context made by
    # Homebrew's libunwind (see cmake/toolchains/homebrew-llvm.cmake): a pointer
    # authentication trap (SIGTRAP) on the first exception thrown through such
    # a frame. Without the cleanups, TSan's shadow stack can keep stale frames
    # after an exception; reports stay correct otherwise.
    if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            "$<$<CONFIG:ThreadSanitizer>:SHELL:-mllvm -tsan-handle-cxx-exceptions=0>")
    endif()
    message(STATUS "sanitizers: ThreadSanitizer instruments with TSan")
endif()
