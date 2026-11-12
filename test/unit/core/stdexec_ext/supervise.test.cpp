#include "core/stdexec_ext/supervise.hpp"

#include <catch2/catch_test_macros.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <exception>
#include <stdexcept>
#include <string>

using namespace redis::stdexec_ext;
namespace ex = stdexec;

namespace {
/// A sender that could succeed but fails, with a runtime_error saying \p what.
/// (sync_wait only takes senders that can succeed.)
auto failing(const char *what) {
	return ex::just() | ex::then([what] { throw std::runtime_error(what); });
}

/// A sender that could succeed but is stopped.
auto stopping() {
	using Sender = exec::variant_sender<decltype(ex::just()),
										decltype(ex::just_stopped())>;
	return Sender(ex::just_stopped());
}

/// What the error in \p error says.
std::string message(const std::exception_ptr &error) {
	try {
		std::rethrow_exception(error);
	} catch (const std::exception &e) { return e.what(); }
}
} // namespace

SCENARIO("An isolated failure is handled where it happens",
		 "[supervise][isolate]") {
	GIVEN("a sender that fails") {
		int calls = 0;
		std::string seen;
		auto handler = [&](const std::exception_ptr &e) noexcept {
			++calls;
			seen = message(e);
		};

		WHEN("it is supervised with Isolate") {
			auto result = ex::sync_wait(
				supervised<FailPolicy::Isolate>(failing("boom"), handler));

			THEN("the handler sees the error once and the sender completes") {
				CHECK(result.has_value());
				CHECK(calls == 1);
				CHECK(seen == "boom");
			}
		}
	}

	GIVEN("a sender that succeeds") {
		int calls = 0;
		auto handler = [&](const std::exception_ptr &) noexcept { ++calls; };

		WHEN("it is supervised with Isolate") {
			auto result = ex::sync_wait(
				supervised<FailPolicy::Isolate>(ex::just(), handler));

			THEN("it completes and the handler is not called") {
				CHECK(result.has_value());
				CHECK(calls == 0);
			}
		}
	}

	GIVEN("a sender that is stopped") {
		int calls = 0;
		auto handler = [&](const std::exception_ptr &) noexcept { ++calls; };

		WHEN("it is supervised with Isolate") {
			auto result = ex::sync_wait(
				supervised<FailPolicy::Isolate>(stopping(), handler));

			THEN("the stop passes through and is not a failure") {
				CHECK(!result.has_value());
				CHECK(calls == 0);
			}
		}
	}
}

SCENARIO("A fail-fast failure propagates", "[supervise][failfast]") {
	GIVEN("a sender that fails") {
		WHEN("it is supervised with FailFast") {
			THEN("the error reaches whoever waits for it") {
				CHECK_THROWS_AS(ex::sync_wait(supervised<FailPolicy::FailFast>(
									failing("boom"))),
								std::runtime_error);
			}
		}
	}

	GIVEN("a sender that fails, and an observer") {
		int calls = 0;
		std::string seen;
		auto observer = [&](const std::exception_ptr &e) noexcept {
			++calls;
			seen = message(e);
		};

		WHEN("it is supervised with FailFast") {
			THEN("the observer sees the error once, and it still propagates") {
				CHECK_THROWS_AS(ex::sync_wait(supervised<FailPolicy::FailFast>(
									failing("boom"), observer)),
								std::runtime_error);
				CHECK(calls == 1);
				CHECK(seen == "boom");
			}
		}
	}

	GIVEN("a sender that is stopped") {
		WHEN("it is supervised with FailFast") {
			auto result = ex::sync_wait(
				supervised<FailPolicy::FailFast>(stopping()));

			THEN("the stop passes through") { CHECK(!result.has_value()); }
		}
	}
}
