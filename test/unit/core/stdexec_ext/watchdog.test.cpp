#include "core/stdexec_ext/watchdog.hpp"

#include "core/chrono/elapsed_timer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <exec/timed_thread_scheduler.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace redis::stdexec_ext;
using namespace std::chrono_literals;
namespace ex = stdexec;

namespace {
/// Work that takes \p d, timed on \p scheduler: it waits, as a slow
/// operation would, and a stop request ends the wait.
auto slow(exec::timed_thread_scheduler scheduler, std::chrono::milliseconds d) {
	return exec::schedule_after(scheduler, d) | ex::then([] { return 42; });
}

/// What the heartbeat tests describe work with: a name and a size, as the
/// server describes a command.
struct Job {
	std::string_view name;
	std::size_t size = 0;
};

using Clock = std::chrono::steady_clock;
} // namespace

SCENARIO("watched() reports work still running past the threshold",
		 "[watchdog][watched]") {
	// One thread for the work, one for the watch: the watch must not share
	// the work's thread.
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
			ex::sync_wait(watched<ReportPolicy::Once>(ex::just(7),
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
			ex::sync_wait(watched<ReportPolicy::Once>(slow(work_on, 50ms),
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
		ex::sync_wait(watched<ReportPolicy::EveryPeriod>(slow(work_on, 60ms),
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
				ex::sync_wait(watched<ReportPolicy::Once>(std::move(failing),
														  watch_on,
														  1h,
														  on_stall)),
				std::runtime_error);
			CHECK(reports == 0);
		}
	}

	GIVEN("work stopped from outside before it finishes") {
		// The other side wins the race, which stops the watched work.
		ex::sync_wait(
			exec::when_any(watched<ReportPolicy::Once>(slow(work_on, 1h) |
														   ex::then([](int) {}),
													   watch_on,
													   1h,
													   on_stall),
						   exec::schedule_after(work_on, 5ms)));
		THEN("the stop reaches the work, and nothing is reported") {
			CHECK(reports == 0);
		}
	}

	GIVEN("the adaptor form") {
		const auto [value] =
			ex::sync_wait(slow(work_on, 50ms) |
						  watched<ReportPolicy::Once>(watch_on, 5ms, on_stall))
				.value();
		THEN("it is the same watch") {
			CHECK(value == 42);
			CHECK(reports == 1);
		}
	}
}

SCENARIO("time_limited() abandons work that runs past its limit",
		 "[watchdog][time_limited]") {
	exec::timed_thread_context work_context;
	exec::timed_thread_context timer_context;
	const auto work_on  = work_context.get_scheduler();
	const auto timer_on = timer_context.get_scheduler();

	std::atomic<int> expiries{0};
	const auto on_expiry = [&expiries] {
		++expiries;
		return -1;
	};

	GIVEN("work that finishes within the limit") {
		const auto [value] =
			ex::sync_wait(
				time_limited(slow(work_on, 1ms), timer_on, 1h, on_expiry))
				.value();
		THEN("its value comes through, and the limit never ran out") {
			CHECK(value == 42);
			CHECK(expiries == 0);
		}
	}

	GIVEN("work that would run past the limit") {
		const redis::ElapsedTimer timer;
		const auto [value] =
			ex::sync_wait(
				time_limited(slow(work_on, 1h), timer_on, 10ms, on_expiry))
				.value();
		THEN("it is cancelled at the limit, ending with on_expiry's value") {
			CHECK(value == -1);
			CHECK(expiries == 1);
			// The limit is 10ms; this only rules out waiting for the work.
			CHECK(timer.elapsed() < 1s);
		}
	}

	GIVEN("work that fails within the limit") {
		auto failing = ex::just() | ex::then([]() -> int {
						   throw std::runtime_error("boom");
					   });
		THEN("the error comes through") {
			CHECK_THROWS_AS(
				ex::sync_wait(
					time_limited(std::move(failing), timer_on, 1h, on_expiry)),
				std::runtime_error);
			CHECK(expiries == 0);
		}
	}

	GIVEN("the adaptor form") {
		const auto [value] =
			ex::sync_wait(slow(work_on, 1h) |
						  time_limited(timer_on, 10ms, on_expiry))
				.value();
		THEN("it is the same race") {
			CHECK(value == -1);
			CHECK(expiries == 1);
		}
	}
}

SCENARIO("escalate() reports slow work, then abandons it",
		 "[watchdog][escalate]") {
	exec::timed_thread_context work_context;
	exec::timed_thread_context watch_context;
	const auto work_on  = work_context.get_scheduler();
	const auto watch_on = watch_context.get_scheduler();

	std::atomic<int> reports{0};
	std::atomic<int> expiries{0};
	const auto on_stall =
		[&reports](std::chrono::steady_clock::duration) noexcept { ++reports; };
	const auto on_expiry = [&expiries] {
		++expiries;
		return -1;
	};
	const Escalation<std::chrono::milliseconds> limits{.report_after = 5ms,
													   .cancel_after = 50ms};

	GIVEN("work that ends between the two limits") {
		const auto [value] =
			ex::sync_wait(escalate<ReportPolicy::Once>(slow(work_on, 20ms),
													   watch_on,
													   limits,
													   on_stall,
													   on_expiry))
				.value();
		THEN("it is reported, and finishes") {
			CHECK(value == 42);
			CHECK(reports == 1);
			CHECK(expiries == 0);
		}
	}

	GIVEN("work that runs past both") {
		const auto [value] =
			ex::sync_wait(slow(work_on, 1h) |
						  escalate<ReportPolicy::Once>(watch_on,
													   limits,
													   on_stall,
													   on_expiry))
				.value();
		THEN("it is reported, then abandoned") {
			CHECK(value == -1);
			CHECK(reports == 1);
			CHECK(expiries == 1);
		}
	}

	GIVEN("work that ends before either") {
		const auto [value] =
			ex::sync_wait(escalate<ReportPolicy::Once>(ex::just(7),
													   watch_on,
													   limits,
													   on_stall,
													   on_expiry))
				.value();
		THEN("nothing happens to it") {
			CHECK(value == 7);
			CHECK(reports == 0);
			CHECK(expiries == 0);
		}
	}
}

SCENARIO("A heartbeat slot says what work is running, consistently",
		 "[watchdog][heartbeat]") {
	HeartbeatSlot<Job, Clock> slot;
	const auto t0 = Clock::now();

	GIVEN("a slot that has never beaten") {
		THEN("it reads as idle") { CHECK_FALSE(slot.read()); }
	}

	GIVEN("work that has started") {
		slot.beat({.name = "SORT", .size = 3}, t0);
		THEN("it reads back whole, with when it started") {
			const auto beat = slot.read();
			REQUIRE(beat);
			CHECK(beat->subject.name == "SORT");
			CHECK(beat->subject.size == 3);
			CHECK(beat->started == t0);
		}
		WHEN("it ends") {
			slot.rest();
			THEN("the slot reads as idle again") { CHECK_FALSE(slot.read()); }
		}
	}

	GIVEN("two pieces of work, one after the other") {
		slot.beat({.name = "A", .size = 1}, t0);
		const auto first = slot.read();
		slot.rest();
		slot.beat({.name = "B", .size = 2}, t0);
		const auto second = slot.read();
		THEN("their generations tell them apart, in order") {
			REQUIRE(first);
			REQUIRE(second);
			CHECK(second->generation > first->generation);
		}
	}

	GIVEN("a reader while the writer beats as fast as it can") {
		// Each name goes with its own size: a read that mixed two beats would
		// pair a name with the other's size.
		constexpr std::string_view LONG  = "ZRANGEBYSCORE";
		constexpr std::string_view SHORT = "GET";
		std::atomic<bool> done{false};
		std::thread writer([&] {
			for (int i = 0; i < 200'000; ++i) {
				const auto name = i % 2 == 0 ? LONG : SHORT;
				slot.beat({.name = name, .size = name.size()}, t0);
				slot.rest();
			}
			done = true;
		});
		int mixed = 0;
		while (!done) {
			if (const auto beat = slot.read())
				if (beat->subject.name.size() != beat->subject.size) ++mixed;
		}
		writer.join();
		THEN("no read mixes two beats") { CHECK(mixed == 0); }
	}
}

// The detector is judged at times the test chooses, so these need neither a
// thread nor real time to pass.
SCENARIO("A stall detector reports work past its threshold",
		 "[watchdog][heartbeat]") {
	HeartbeatSlot<Job, Clock> slot;
	const auto t0 = Clock::now();

	GIVEN("ReportPolicy::Once") {
		StallDetector<Job, Clock> detector(slot, ReportPolicy::Once, 50ms);
		slot.beat({.name = "KEYS", .size = 2}, t0);
		THEN("it is not reported before the threshold") {
			CHECK_FALSE(detector.check(t0 + 10ms));
		}
		THEN("it is reported once past it, with how long it ran") {
			const auto stall = detector.check(t0 + 60ms);
			REQUIRE(stall);
			CHECK(stall->subject.name == "KEYS");
			CHECK(stall->running == 60ms);
			CHECK_FALSE(detector.check(t0 + 1s));
		}
		THEN("the next piece of work is reported too") {
			REQUIRE(detector.check(t0 + 60ms));
			slot.beat({.name = "SORT", .size = 2}, t0 + 100ms);
			const auto stall = detector.check(t0 + 200ms);
			REQUIRE(stall);
			CHECK(stall->subject.name == "SORT");
		}
	}

	GIVEN("ReportPolicy::EveryPeriod") {
		StallDetector<Job, Clock> detector(slot,
										   ReportPolicy::EveryPeriod,
										   50ms);
		slot.beat({.name = "KEYS", .size = 2}, t0);
		THEN("it is reported at every look past the threshold") {
			const auto first  = detector.check(t0 + 60ms);
			const auto second = detector.check(t0 + 160ms);
			REQUIRE(first);
			REQUIRE(second);
			CHECK(second->running == 160ms);
		}
	}
}

SCENARIO("monitor() reports work stuck past its threshold",
		 "[watchdog][heartbeat]") {
	exec::timed_thread_context watch_context;
	exec::timed_thread_context timer_context;
	HeartbeatSlot<Job, Clock> slot;
	std::atomic<int> reports{0};
	const auto on_stall = [&reports](const Stall<Job, Clock> &) noexcept {
		++reports;
	};

	GIVEN("work that started long ago, under Once") {
		slot.beat({.name = "DEBUG", .size = 2}, Clock::now() - 1s);
		WHEN("the monitor runs for a while, then is stopped") {
			// The other side wins the race, which stops the monitor.
			ex::sync_wait(exec::when_any(
				monitor(slot,
						watch_context.get_scheduler(),
						1ms,
						ReportPolicy::Once,
						std::chrono::milliseconds(1),
						on_stall),
				exec::schedule_after(timer_context.get_scheduler(), 50ms)));
			THEN("it was reported once") { CHECK(reports == 1); }
		}
	}

	GIVEN("no work running") {
		ex::sync_wait(exec::when_any(
			monitor(slot,
					watch_context.get_scheduler(),
					1ms,
					ReportPolicy::EveryPeriod,
					std::chrono::milliseconds(1),
					on_stall),
			exec::schedule_after(timer_context.get_scheduler(), 20ms)));
		THEN("nothing is reported") { CHECK(reports == 0); }
	}
}
