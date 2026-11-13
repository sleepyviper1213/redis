#include "server/diagnostics/command_watchdog.hpp"

#include <catch2/catch_test_macros.hpp>
#include <exec/timed_thread_scheduler.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <string>

using namespace redis;
using namespace redis::diagnostics;
using namespace std::chrono_literals;

// The detector is judged at times the test chooses, so these need neither a
// thread nor real time to pass.
SCENARIO("A command running past the threshold is reported once",
		 "[watchdog]") {
	StallDetector detector(command_heartbeat(), ReportPolicy::Once, 50ms);
	const auto t0 = TickClock::now();

	GIVEN("no command running") {
		clear_current_command();
		THEN("nothing is reported, however late") {
			CHECK_FALSE(detector.check(t0 + 1h));
		}
	}

	GIVEN("a command that started at t0") {
		set_current_command("DEBUG", 2, t0);

		THEN("it is not reported before the threshold") {
			CHECK_FALSE(detector.check(t0 + 10ms));
		}
		WHEN("the threshold has passed") {
			const auto stall = detector.check(t0 + 60ms);
			THEN("it is reported with its name, size and how long it ran") {
				REQUIRE(stall);
				CHECK(stall->subject.name == "DEBUG");
				CHECK(stall->subject.argc == 2);
				CHECK(stall->running == 60ms);
			}
			AND_THEN("it is not reported again while it keeps running") {
				CHECK_FALSE(detector.check(t0 + 1s));
			}
		}
		WHEN("it ends before the watchdog looks") {
			clear_current_command();
			THEN("nothing is reported") {
				CHECK_FALSE(detector.check(t0 + 1s));
			}
		}
		clear_current_command();
	}

	GIVEN("one long command reported, then another") {
		set_current_command("KEYS", 2, t0);
		REQUIRE(detector.check(t0 + 60ms));
		set_current_command("SORT", 2, t0 + 100ms);
		THEN("the second one is reported too") {
			const auto stall = detector.check(t0 + 200ms);
			REQUIRE(stall);
			CHECK(stall->subject.name == "SORT");
		}
		clear_current_command();
	}
}

SCENARIO("Under EveryPeriod, a stuck command is reported at every look",
		 "[watchdog]") {
	StallDetector detector(command_heartbeat(),
						   ReportPolicy::EveryPeriod,
						   50ms);
	const auto t0 = TickClock::now();
	set_current_command("DEBUG", 2, t0);

	THEN("it is not reported before the threshold") {
		CHECK_FALSE(detector.check(t0 + 10ms));
	}
	THEN("it is reported at each look past it, with how long it has run") {
		const auto first  = detector.check(t0 + 60ms);
		const auto second = detector.check(t0 + 160ms);
		REQUIRE(first);
		REQUIRE(second);
		CHECK(first->running == 60ms);
		CHECK(second->running == 160ms);
	}
	clear_current_command();
}

SCENARIO("The watchdog reports a stalled command while it runs", "[watchdog]") {
	GIVEN("no threshold") {
		CommandWatchdog dog(ReportPolicy::Once,
							{.threshold = 0ms},
							[](const Stall &) {});
		THEN("it is disabled, and run() completes at once") {
			CHECK_FALSE(dog.is_enabled());
			stdexec::sync_wait(dog.run());
		}
	}

	GIVEN("a threshold, and a command that started long ago") {
		std::atomic<int> reports{0};
		std::string name;
		CommandWatchdog dog(ReportPolicy::Once,
							{.threshold = 1ms, .period = 1ms},
							[&](const Stall &stall) {
								name = stall.subject.name;
								++reports;
							});
		set_current_command("DEBUG", 2, TickClock::now() - 1s);

		WHEN("it runs for a while, then is stopped") {
			// The deadline wins the race, which stops the watchdog.
			exec::timed_thread_context timer;
			stdexec::sync_wait(exec::when_any(
				dog.run(),
				exec::schedule_after(timer.get_scheduler(), 50ms)));
			THEN("the command was reported once, by name") {
				CHECK(reports == 1);
				CHECK(name == "DEBUG");
			}
		}
		clear_current_command();
	}
}
