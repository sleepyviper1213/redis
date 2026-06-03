#include <catch2/catch_test_macros.hpp>

#include <csetjmp>
#include <csignal>

namespace detail {
thread_local bool sigtrap_caught = false;

// inline void sigtrap_handler(int) {
// sigtrap_caught = true;
// longjmp(sigjmp_env, 1);
//}
} // namespace detail

// NOLINTBEGIN(cppcoreguidelines-*)
#define REQUIRE_PANIC(statement)                                               \
	do {                                                                       \
		using namespace detail;                                                \
		sigtrap_caught = false;                                                \
		std::signal(SIGTRAP, sigtrap_handler);                                 \
		if (setjmp(sigjmp_env) == 0) {                                         \
			statement;                                                         \
			FAIL("Expected SIGTRAP but none was raised");                      \
		} else {                                                               \
			REQUIRE(sigtrap_caught);                                           \
		}                                                                      \
		std::signal(SIGTRAP, SIG_DFL);                                         \
		panic_message = nullptr;                                               \
	} while (0)

// NOLINTEND(cppcoreguidelines-*)
