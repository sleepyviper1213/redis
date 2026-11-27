#pragma once
// Whether this build has a sandbox (ADR-0014): Linux on x86-64, the only
// architecture whose allow-list is written and tested, and not under a
// sanitizer whose runtime makes system calls of its own (shadow memory,
// symbolizers). UBSan stays sandboxed: its runtime is silent until it reports.

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) ||     \
	__has_feature(memory_sanitizer) || __has_feature(realtime_sanitizer)
#define REDIS_SANITIZER_RUNTIME 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define REDIS_SANITIZER_RUNTIME 1
#endif

#if defined(__linux__) && defined(__x86_64__) &&                               \
	!defined(REDIS_SANITIZER_RUNTIME)
#define REDIS_HAS_SANDBOX 1
#else
#define REDIS_HAS_SANDBOX 0
#endif
