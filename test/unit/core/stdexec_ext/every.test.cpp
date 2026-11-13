#include "core/stdexec_ext/every.hpp"

#include <catch2/catch_test_macros.hpp>
#include <exec/timed_thread_scheduler.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace redis::stdexec_ext;
using namespace std::chrono_literals;
namespace ex = stdexec;

SCENARIO("every() calls its step each period until it is stopped", "[every]") {
	exec::timed_thread_context context;
	const auto scheduler = context.get_scheduler();
	std::atomic<int> steps{0};
	const auto step = [&steps]() noexcept { ++steps; };

	GIVEN("a loop raced against a later deadline") {
		// The deadline wins the race, which stops the loop: the only way it
		// ends.
		ex::sync_wait(exec::when_any(every(scheduler, 1ms, step),
									 exec::schedule_after(scheduler, 50ms)));
		THEN("the step ran several times, and runs no more") {
			const int seen = steps;
			CHECK(seen > 1);
			std::this_thread::sleep_for(10ms);
			CHECK(steps == seen);
		}
	}

	GIVEN("a loop whose first period is longer than the race") {
		ex::sync_wait(exec::when_any(every(scheduler, 1h, step),
									 exec::schedule_after(scheduler, 1ms)));
		THEN("the step never ran: the first call waits a period") {
			CHECK(steps == 0);
		}
	}
}
