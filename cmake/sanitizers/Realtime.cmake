include_guard(GLOBAL)

# RealtimeSanitizer (clang 20+) as its own RelWithDebInfo-based config. It
# aborts when a function marked [[clang::nonblocking]] reaches malloc, a lock,
# a syscall that blocks, and the like. It cannot share a binary with ASan or
# TSan, hence its own configuration. The runtime ships with clang for Linux and
# macOS; MSVC, MinGW and GCC have none.

_redis_register_sanitizer_config(RealtimeSanitizer)

if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    _redis_sanitizer_uninstrumented(RealtimeSanitizer
        "RealtimeSanitizer needs clang's runtime on Linux or macOS")
else()
    _redis_add_sanitizer_flags(RealtimeSanitizer
        -fsanitize=realtime -fno-omit-frame-pointer -g)
    message(STATUS "sanitizers: RealtimeSanitizer instruments with RTSan")
endif()
