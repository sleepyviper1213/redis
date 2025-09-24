#pragma once
#include <fmt/base.h>

#include <csetjmp>
#include <csignal>
#include <cstdlib>

#ifdef __cpp_lib_stacktrace
#include <stacktrace>
#else
#include <cxxabi.h>
#include <execinfo.h>
#endif

namespace AK {

thread_local const char *panic_message = nullptr;
thread_local jmp_buf sigjmp_env;

// Panic function: raises SIGTRAP for tests or terminates in runtime
extern "C" [[noreturn]] inline void panic(const char *message) {
	panic_message = message;
	std::raise(SIGTRAP);
	std::exit(1); // fallback if signal not caught
}

#ifndef __cpp_lib_stacktrace
inline void dump_backtrace() {
	constexpr int max_frames = 256;
	void *trace[max_frames]  = {};
	int num_frames           = backtrace(trace, max_frames);
	char **syms              = backtrace_symbols(trace, num_frames);

	fmt::println(stderr, "Stack trace:");
	for (int i = 0; i < num_frames; ++i) {
		char *mangled   = syms[i];
		char *demangled = nullptr;
		size_t len      = 0;
		int status      = 0;

		demangled = abi::__cxa_demangle(mangled, nullptr, &len, &status);

		if (status == 0 && demangled) {
			fmt::println(stderr, "{}", demangled);
			std::free(demangled);
		} else {
			fmt::println(stderr, "{}", mangled);
		}
	}
	std::free(syms);
}
#endif

// Signal handler that prints panic message and optional backtrace
inline void sigtrap_handler(int signum) {
	if (signum != SIGTRAP) return;

	if (panic_message) {
		fmt::println(stderr, "\033[31;1mPANIC\033[0m: {}", panic_message);

		if (std::getenv("DUMP_BACKTRACE")) {
#ifdef __cpp_lib_stacktrace
			fmt::println(stderr,
						 "Stack trace:\n{}",
						 std::stacktrace::current());
#else
			dump_backtrace();
#endif
		}

		longjmp(sigjmp_env, 1); // return to test
	}
}

// Helper macros
#define __stringify_helper(x) #x
#define __stringify(x) __stringify_helper(x)

#define VERIFY(expr)                                                           \
	if (!(expr)) [[unlikely]] {                                                \
		panic(#expr " at " __FILE__ ":" __stringify(__LINE__));                \
	}

} // namespace AK
