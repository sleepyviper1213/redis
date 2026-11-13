#include "core/stdexec_ext/watchdog.hpp"

#include <catch2/catch_test_macros.hpp>
#include <exec/timed_thread_scheduler.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>

using namespace redis::stdexec_ext;
using namespace std::chrono_literals;
namespace ex = stdexec;

namespace {
/// Work that takes \p d, timed on \p scheduler: it waits, as a slow
/// operation would, and a stop request ends the wait.
auto slow(exec::timed_thread_scheduler scheduler, std::chrono::milliseconds d) {
	return exec::schedule_after(scheduler, d) | ex::then([] { return 42; });
}
} // namespace

SCENARIO("watchdog() reports work still running past the threshold",
		 "[watchdog]") {
	// One thread for the work, one for the watchdog: the watchdog must not
	// share the work's thread.
	exec::timed_thread_context work_context;
	exec::timed_thread_context watch_context;
	const auto work_on  = work_context.get_scheduler();
	const auto watch_on = watch_context.get_scheduler();

	std::atomic<int> reports{0};
	std::atomic<std::chrono::steady_clock::rep> last{0};
	const auto on_stall =
		[&](std::chrono::steady_clock::duration ran) noexcept {
			last = ran.count();
			++reports;
		};

	GIVEN("work that finishes before the threshold") {
		const auto [value] =
			ex::sync_wait(watchdog<ReportPolicy::Once>(ex::just(7),
													   watch_on,
													   1h,
													   on_stall))
				.value();
		THEN("its value comes through, at once and unreported") {
			CHECK(value == 7);
			CHECK(reports == 0);
		}
	}

	GIVEN("work that runs past the threshold, under Once") {
		const auto [value] =
			ex::sync_wait(watchdog<ReportPolicy::Once>(slow(work_on, 50ms),
													   watch_on,
													   5ms,
													   on_stall))
				.value();
		THEN("it is reported once, with how long it had run, and completes") {
			CHECK(value == 42);
			CHECK(reports == 1);
			CHECK(std::chrono::steady_clock::duration(last.load()) >= 5ms);
		}
	}

	GIVEN("work that runs past the threshold, under EveryPeriod") {
		ex::sync_wait(watchdog<ReportPolicy::EveryPeriod>(slow(work_on, 60ms),
														  watch_on,
														  5ms,
														  on_stall));
		THEN("it is reported again while it keeps running") {
			CHECK(reports > 1);
		}
	}

	GIVEN("work that fails") {
		auto failing = ex::just() | ex::then([]() -> int {
						   throw std::runtime_error("boom");
					   });
		THEN("the error comes through, unreported") {
			CHECK_THROWS_AS(
				ex::sync_wait(watchdog<ReportPolicy::Once>(std::move(failing),
														   watch_on,
														   1h,
														   on_stall)),
				std::runtime_error);
			CHECK(reports == 0);
		}
	}

	GIVEN("work stopped from outside before it finishes") {
		// The deadline wins the race, which stops the watched work.
		ex::sync_wait(
			exec::when_any(watchdog<ReportPolicy::Once>(
							   slow(work_on, 1h) | ex::then([](int) {}),
							   watch_on,
							   1h,
							   on_stall),
						   exec::schedule_after(work_on, 5ms)));
		THEN("the stop reaches the work, and nothing is reported") {
			CHECK(reports == 0);
		}
	}
}
