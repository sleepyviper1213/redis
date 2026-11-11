#include "core/stdexec_ext/retry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexec/execution.hpp>

#include <exception>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

using namespace redis::stdexec_ext;
namespace ex = stdexec;

namespace {
/// An attempt that throws until it has been tried \p succeed_on times.
AttemptFactory auto flaky(int &calls, int succeed_on) {
	return [&calls, succeed_on] {
		return ex::just() | ex::then([&calls, succeed_on] {
				   if (++calls < succeed_on) throw std::runtime_error("flaky");
				   return 42;
			   });
	};
}
} // namespace

SCENARIO("A failed sender is tried again", "[retry]") {
	GIVEN("an attempt that succeeds on its third try") {
		int calls = 0;

		WHEN("the policy allows enough attempts") {
			auto result = ex::sync_wait(retry(flaky(calls, 3), retry_times(5)));

			THEN("it completes with the successful attempt's value") {
				REQUIRE(result.has_value());
				CHECK(std::get<0>(*result) == 42);
				CHECK(calls == 3);
			}
		}

		WHEN("the policy allows only two attempts") {
			THEN("it completes with the last error, after exactly two tries") {
				CHECK_THROWS_AS(ex::sync_wait(retry(flaky(calls, 3), retry_times(2))),
								std::runtime_error);
				CHECK(calls == 2);
			}
		}
	}

	GIVEN("an attempt that succeeds at once") {
		int calls = 0;
		auto result = ex::sync_wait(retry(flaky(calls, 1), retry_times(3)));

		THEN("it is tried once and the policy is never asked") {
			REQUIRE(result.has_value());
			CHECK(std::get<0>(*result) == 42);
			CHECK(calls == 1);
		}
	}

	GIVEN("an attempt with no value") {
		int calls     = 0;
		AttemptFactory auto make     = [&calls] {
			return ex::just() | ex::then([&calls] {
					   if (++calls < 2) throw std::runtime_error("flaky");
				   });
		};
		auto result = ex::sync_wait(retry(make, retry_times(3)));

		THEN("retry completes with no value either") {
			REQUIRE(result.has_value());
			CHECK(calls == 2);
		}
	}
}

SCENARIO("The policy sees every failure", "[retry]") {
	GIVEN("a policy that records what it is told") {
		int calls = 0;
		std::vector<size_t> failures;
		std::vector<std::string> reasons;
		RetryPolicy auto policy = [&](size_t count, const std::exception_ptr &error) {
			failures.push_back(count);
			try {
				std::rethrow_exception(error);
			} catch (const std::exception &e) {
				reasons.emplace_back(e.what());
			}
			return ex::just(true);
		};

		WHEN("the attempt fails twice before succeeding") {
			auto result = ex::sync_wait(retry(flaky(calls, 3), policy));

			THEN("it hears of each failure, counted from one, with its error") {
				REQUIRE(result.has_value());
				CHECK(failures == std::vector<size_t>{1, 2});
				CHECK(reasons == std::vector<std::string>{"flaky", "flaky"});
			}
		}
	}

	GIVEN("an attempt that fails with an error code rather than an exception") {
		int calls = 0;
		AttemptFactory auto make = [&calls] {
			++calls;
			return ex::just_error(
				std::make_error_code(std::errc::connection_refused));
		};

		THEN("the policy and the final error still see an exception") {
			CHECK_THROWS_AS(ex::sync_wait(retry(make, retry_times(2))),
							std::system_error);
			CHECK(calls == 2);
		}
	}
}

SCENARIO("A retry that cannot overflow the stack", "[retry]") {
	GIVEN("an attempt that fails immediately, many times in a row") {
		const auto error = std::make_exception_ptr(std::runtime_error("down"));
		size_t calls     = 0;
		AttemptFactory auto make        = [&] {
            ++calls;
            return ex::just_error(error);
		};
		constexpr size_t ATTEMPTS = 100'000;

		THEN("it gives up cleanly after the last one") {
			CHECK_THROWS_AS(ex::sync_wait(retry(make, retry_times(ATTEMPTS))),
							std::runtime_error);
			CHECK(calls == ATTEMPTS);
		}
	}
}

SCENARIO("A stop is never retried", "[retry]") {
	GIVEN("a policy whose wait is stopped") {
		int calls   = 0;
		RetryPolicy auto policy = [](size_t, const std::exception_ptr &) {
			return ex::just_stopped() | ex::then([] { return true; });
		};

		THEN("the retry is stopped, with no further attempt") {
			auto result = ex::sync_wait(retry(flaky(calls, 3), policy));
			CHECK_FALSE(result.has_value());
			CHECK(calls == 1);
		}
	}

	GIVEN("an attempt that is itself stopped") {
		int calls = 0;
		AttemptFactory auto make = [&calls] {
			++calls;
			return ex::just_stopped() | ex::then([] { return 1; });
		};

		THEN("it is not tried again") {
			auto result = ex::sync_wait(retry(make, retry_times(5)));
			CHECK_FALSE(result.has_value());
			CHECK(calls == 1);
		}
	}
}
