#pragma once

#include <fmt/std.h>

#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <string_view>

// An assertion that stays on in release builds.
//
// assert() vanishes under NDEBUG, so the condition it guards becomes undefined
// behaviour in the build that runs in production. REDIS_VERIFY states a
// condition the program cannot continue without: if it is false the process
// reports where and why, then aborts, and the crash handler (see
// server/diagnostics/crash.hpp) turns the SIGABRT into the bug report and core
// dump. Use it for broken invariants, never for a failure the caller can
// handle: that is a std::expected.
//
//   REDIS_VERIFY(index < size_);
//   REDIS_VERIFY(fd >= 0, "the log descriptor is opened at install time");
//
// The condition is evaluated exactly once. The message is optional and must be
// a string literal or string_view.
namespace redis::detail {

[[noreturn]] inline void verify_failed(const char *condition,
									   std::string_view message,
									   const std::source_location &where) {
	fmt::println(stderr, "\nVERIFY failed: {}\n  at {}", condition, where);
	if (!message.empty()) fmt::println(stderr, "  {}", message);
	std::fflush(stderr);
	std::abort();
}

} // namespace redis::detail

#ifndef REDIS_VERIFY
#define REDIS_VERIFY(condition, ...)                                           \
	do {                                                                       \
		if (!(condition)) [[unlikely]]                                         \
			::redis::detail::verify_failed(#condition,                         \
										   std::string_view{__VA_ARGS__},      \
										   std::source_location::current());   \
	} while (false)
#endif
