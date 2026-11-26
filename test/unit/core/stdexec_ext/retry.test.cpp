#include "core/stdexec_ext/retry.hpp"
#include "core/stdexec_ext/watchdog/fail_after.hpp"
#include "core/stdexec_ext/watchdog/time_limited.hpp"

#include "core/chrono/elapsed_timer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <exec/timed_thread_scheduler.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace redis::stdexec_ext;
using namespace std::chrono_literals;
namespace ex = stdexec;

namespace {
/// An attempt that throws until it has been tried \p succeed_on times.
AttemptFunc auto flaky(int &calls, int succeed_on) {
	return [&calls, succeed_on] {
		return ex::just() | ex::then([&calls, succeed_on] {
				   if (++calls < succeed_on) throw std::runtime_error("flaky");
				   return 42;
			   });
	};
}

/// An attempt that asks \p stop for a stop and then fails anyway: work that
/// ignores its stop token, so the stop is seen only after it has failed.
AttemptFunc auto fails_while_stopping(int &calls,
									  stdexec::inplace_stop_source &stop) {
	return [&calls, &stop] {
		return ex::just() | ex::then([&calls, &stop]() -> int {
				   ++calls;
				   stop.request_stop();
				   throw std::runtime_error("failed while stopping");
			   });
	};
}

/// The answer a slow attempt sends: a named type, so that the attempt built
/// in hangs_once() and the one named in its variant are the same type.
struct Answer {
	int operator()() const noexcept { return 42; }
};

/**
 * An attempt that hangs on its first try (an hour's wait that a stop ends)
 * and answers 42 at once on every later one, each try failed after \p limit.
 */
AttemptFunc auto hangs_once(int &calls, exec::timed_thread_scheduler scheduler,
							std::chrono::milliseconds limit) {
	return [&calls, scheduler, limit] {
		using Hang = decltype(exec::schedule_after(scheduler, 1h) |
							  ex::then(Answer{}));
		using Quick   = decltype(ex::just(42));
		using Attempt = exec::variant_sender<Hang, Quick>;

		Attempt attempt =
			++calls == 1
				? Attempt(exec::schedule_after(scheduler, 1h) | ex::then(Answer{}))
				: Attempt(ex::just(42));
		return std::move(attempt) | fail_after(scheduler, limit);
	};
}

/// The error code \p error holds, if it is a std::system_error; none else.
std::error_code code_of(const std::exception_ptr &error) {
	try {
		std::rethrow_exception(error);
	} catch (const std::system_error &e) {
		return e.code();
	} catch (...) {
		return {};
	}
}
} // namespace

SCENARIO("A failed sender is tried again", "[retry]") {
	GIVEN("an attempt that succeeds on its third try") {
		int calls = 0;

		WHEN("the policy allows enough attempts") {
			auto result = ex::sync_wait(retry(flaky(calls, 3), retry_policy::immediately(5)));

			THEN("it completes with the successful attempt's value") {
				REQUIRE(result.has_value());
				CHECK(std::get<0>(*result) == 42);
				CHECK(calls == 3);
			}
		}

		WHEN("the policy allows only two attempts") {
			THEN("it completes with the last error, after exactly two tries") {
				CHECK_THROWS_AS(ex::sync_wait(retry(flaky(calls, 3), retry_policy::immediately(2))),
								std::runtime_error);
				CHECK(calls == 2);
			}
		}
	}

	GIVEN("an attempt that succeeds at once") {
		int calls = 0;
		auto result = ex::sync_wait(retry(flaky(calls, 1), retry_policy::immediately(3)));

		THEN("it is tried once and the policy is never asked") {
			REQUIRE(result.has_value());
			CHECK(std::get<0>(*result) == 42);
			CHECK(calls == 1);
		}
	}

	GIVEN("an attempt with no value") {
		int calls     = 0;
		AttemptFunc auto make     = [&calls] {
			return ex::just() | ex::then([&calls] {
					   if (++calls < 2) throw std::runtime_error("flaky");
				   });
		};
		auto result = ex::sync_wait(retry(make, retry_policy::immediately(3)));

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
		RetryFunc auto policy = [&](size_t count, const std::exception_ptr &error) {
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
		AttemptFunc auto make = [&calls] {
			++calls;
			return ex::just_error(
				std::make_error_code(std::errc::connection_refused));
		};

		THEN("the policy and the final error still see an exception") {
			CHECK_THROWS_AS(ex::sync_wait(retry(make, retry_policy::immediately(2))),
							std::system_error);
			CHECK(calls == 2);
		}
	}
}

SCENARIO("A retry that cannot overflow the stack", "[retry]") {
	GIVEN("an attempt that fails immediately, many times in a row") {
		const auto error = std::make_exception_ptr(std::runtime_error("down"));
		size_t calls     = 0;
		AttemptFunc auto make        = [&] {
            ++calls;
            return ex::just_error(error);
		};
		constexpr size_t ATTEMPTS = 100'000;

		THEN("it gives up cleanly after the last one") {
			CHECK_THROWS_AS(ex::sync_wait(retry(make, retry_policy::immediately(ATTEMPTS))),
							std::runtime_error);
			CHECK(calls == ATTEMPTS);
		}
	}
}

SCENARIO("A stop is never retried", "[retry]") {
	GIVEN("a policy whose wait is stopped") {
		int calls   = 0;
		RetryFunc auto policy = [](size_t, const std::exception_ptr &) {
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
		AttemptFunc auto make = [&calls] {
			++calls;
			return ex::just_stopped() | ex::then([] { return 1; });
		};

		THEN("it is not tried again") {
			auto result = ex::sync_wait(retry(make, retry_policy::immediately(5)));
			CHECK_FALSE(result.has_value());
			CHECK(calls == 1);
		}
	}
}

SCENARIO("The backoff delay grows to its cap", "[retry]") {
	using retry_policy::backoff_delay;

	GIVEN("an initial delay of 100 ms and a cap of 1 s") {
		THEN("it doubles after each failure until it reaches the cap") {
			STATIC_CHECK(backoff_delay(100ms, 1s, 1) == 100ms);
			STATIC_CHECK(backoff_delay(100ms, 1s, 2) == 200ms);
			STATIC_CHECK(backoff_delay(100ms, 1s, 3) == 400ms);
			STATIC_CHECK(backoff_delay(100ms, 1s, 4) == 800ms);
			STATIC_CHECK(backoff_delay(100ms, 1s, 5) == 1s);
		}

		THEN("it stays at the cap however many failures there were") {
			CHECK(backoff_delay(100ms, 1s, 1000) == 1s);
			CHECK(backoff_delay(100ms, 1s, SIZE_MAX) == 1s);
		}
	}

	GIVEN("an initial delay above the cap") {
		THEN("the first wait is the cap") {
			STATIC_CHECK(backoff_delay(5s, 1s, 1) == 1s);
		}
	}

	GIVEN("no initial delay") {
		THEN("there is never a wait") {
			STATIC_CHECK(backoff_delay(0ms, 1s, 1) == 0ms);
			CHECK(backoff_delay(0ms, 1s, 1000) == 0ms);
		}
	}
}

SCENARIO("A retry with backoff waits longer after each failure", "[retry]") {
	exec::timed_thread_context context;
	auto scheduler = context.get_scheduler();

	GIVEN("an attempt that succeeds on its third try") {
		int calls = 0;

		WHEN("it is retried with backoff") {
			const redis::ElapsedTimer timer;
			auto result = ex::sync_wait(
				retry(flaky(calls, 3),
					  retry_policy::exponential_backoff(scheduler, 5, 20ms, 1s)));
			const auto elapsed = timer.elapsed();

			THEN("it completes with the value, after waiting 20 then 40 ms") {
				REQUIRE(result.has_value());
				CHECK(std::get<0>(*result) == 42);
				CHECK(calls == 3);
				CHECK(elapsed >= 60ms);
			}
		}
	}

	GIVEN("an attempt that always fails, and a long initial delay") {
		int calls = 0;

		WHEN("only one attempt is allowed") {
			const redis::ElapsedTimer timer;
			CHECK_THROWS_AS(
				ex::sync_wait(retry(
					flaky(calls, 1000),
					retry_policy::exponential_backoff(scheduler, 1, 10s, 10s))),
				std::runtime_error);
			const auto elapsed = timer.elapsed();

			THEN("it gives up after that attempt, without waiting") {
				CHECK(calls == 1);
				CHECK(elapsed < 5s);
			}
		}
	}

	GIVEN("an attempt that always fails, and a long wait between attempts") {
		int calls = 0;

		WHEN("a time limit stops the retry during its first wait") {
			const redis::ElapsedTimer timer;
			auto result = ex::sync_wait(time_limited(
				retry(flaky(calls, 1000),
					  retry_policy::exponential_backoff(scheduler, 5, 10s, 10s)),
				scheduler,
				100ms,
				[] { return -1; }));
			const auto elapsed = timer.elapsed();

			THEN("the wait ends at once, with no further attempt") {
				REQUIRE(result.has_value());
				CHECK(std::get<0>(*result) == -1);
				CHECK(calls == 1);
				CHECK(elapsed < 5s);
			}
		}
	}
}

SCENARIO("A stop during an attempt that ignores it ends the retry", "[retry]") {
	GIVEN("an attempt that is stopped while it runs, and fails anyway") {
		ex::inplace_stop_source stop;
		int calls      = 0;
		const auto env = ex::prop{ex::get_stop_token, stop.get_token()};

		WHEN("the policy would try again at once") {
			auto result = ex::sync_wait(
				retry(fails_while_stopping(calls, stop),
					  retry_policy::immediately(3)) |
				ex::write_env(env));

			THEN("the retry is stopped, with no further attempt") {
				CHECK_FALSE(result.has_value());
				CHECK(calls == 1);
			}
		}

		WHEN("the policy would wait a long time first") {
			exec::timed_thread_context context;

			const redis::ElapsedTimer timer;
			auto result = ex::sync_wait(
				retry(fails_while_stopping(calls, stop),
					  retry_policy::exponential_backoff(
						  context.get_scheduler(), 3, 10s, 10s)) |
				ex::write_env(env));
			const auto elapsed = timer.elapsed();

			THEN("the wait ends at once, with no further attempt") {
				CHECK_FALSE(result.has_value());
				CHECK(calls == 1);
				CHECK(elapsed < 5s);
			}
		}
	}
}

SCENARIO("A retry with jittered backoff waits what the jitter source chooses",
		 "[retry]") {
	exec::timed_thread_context context;

	GIVEN("an attempt that fails twice, and a jitter source that keeps a "
		  "hundredth") {
		int calls = 0;
		std::vector<std::chrono::milliseconds> asked;
		const auto hundredth = [&asked](std::chrono::milliseconds upper) {
			asked.push_back(upper);
			return upper / 100;
		};

		WHEN("it is retried with a backoff of 400 ms, doubling") {
			const redis::ElapsedTimer timer;
			auto result = ex::sync_wait(
				retry(flaky(calls, 3),
					  retry_policy::exponential_backoff(
						  context.get_scheduler(), 5, 400ms, 10s, hundredth)));
			const auto elapsed = timer.elapsed();

			THEN("the jitter is asked about every wait, the first included") {
				REQUIRE(result.has_value());
				CHECK(calls == 3);
				CHECK(asked == std::vector<std::chrono::milliseconds>{400ms, 800ms});
			}

			THEN("the retry waits what it chose, not the whole delays") {
				CHECK(elapsed >= 12ms);
				CHECK(elapsed < 600ms);
			}
		}
	}
}

SCENARIO("Full jitter draws from zero to the backoff delay", "[retry]") {
	GIVEN("full jitter over a seeded engine") {
		const auto jitter = retry_policy::full_jitter(std::mt19937_64{42});

		WHEN("it draws many waits for a delay of 100 ms") {
			constexpr int DRAWS = 10'000;
			int outside         = 0;
			int lowest_tenth    = 0;
			int highest_tenth   = 0;
			for (int i = 0; i < DRAWS; ++i) {
				const std::chrono::milliseconds wait = jitter(100ms);
				if (wait < 0ms || wait > 100ms) ++outside;
				if (wait < 10ms) ++lowest_tenth;
				if (wait > 90ms) ++highest_tenth;
			}

			THEN("every wait is within the delay, spread across all of it") {
				CHECK(outside == 0);
				CHECK(lowest_tenth > 0);
				CHECK(highest_tenth > 0);
			}
		}

		THEN("no delay draws no wait") { CHECK(jitter(0ms) == 0ms); }
	}
}

SCENARIO("A hung attempt fails after its limit and is retried", "[retry]") {
	exec::timed_thread_context context;
	const auto scheduler = context.get_scheduler();

	GIVEN("an attempt that hangs on its first try, each try limited to 100 ms") {
		int calls = 0;
		std::vector<std::error_code> seen;
		const RetryFunc auto backoff =
			retry_policy::exponential_backoff(scheduler, 3, 10ms, 1s);
		const RetryFunc auto policy =
			[&seen, backoff](size_t failures, const std::exception_ptr &error) {
				seen.push_back(code_of(error));
				return backoff(failures, error);
			};

		WHEN("it is retried under backoff") {
			const redis::ElapsedTimer timer;
			auto result = ex::sync_wait(
				retry(hangs_once(calls, scheduler, 100ms), policy));

			THEN("the second try answers, after the first timed out") {
				REQUIRE(result.has_value());
				CHECK(std::get<0>(*result) == 42);
				CHECK(calls == 2);
				REQUIRE(seen.size() == 1);
				CHECK(seen[0] == std::errc::timed_out);
				CHECK(timer.elapsed() < 5s);
			}
		}
	}
}
