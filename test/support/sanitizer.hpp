#pragma once

// Whether this test binary runs under ThreadSanitizer, for timing bounds that
// must stretch with it: TSan makes instrumented code several times slower, so
// a guard sized for a plain build can run out before the work it waits for.
//
// GCC predefines __SANITIZE_THREAD__ with -fsanitize=thread; Clang (and
// clang-cl) answers __has_feature(thread_sanitizer) instead.
#if defined(__SANITIZE_THREAD__)
#define REDIS_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define REDIS_TSAN 1
#endif
#endif

#ifndef REDIS_TSAN
#define REDIS_TSAN 0
#endif
