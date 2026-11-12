#include "core/stdexec_ext/supervise.hpp"
#include "core/stdexec_ext/task.hpp"

#include <catch2/catch_test_macros.hpp>
#include <exec/single_thread_context.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <exception>
#include <stdexcept>
#include <thread>

using namespace redis::stdexec_ext;
namespace ex = stdexec;

namespace {
/// The thread it runs on after awaiting \p sched's schedule().
/// \tparam Scheduler The type of the scheduler to hop to.
template <ex::scheduler Scheduler>
InlineTask<std::thread::id> thread_after_hop(Scheduler sched) {
	co_await ex::schedule(sched);
	co_return std::this_thread::get_id();
}

InlineTask<int> answer() { co_return 42; }

InlineTask<void> fail() {
	throw std::runtime_error("boom");
	co_return;
}

InlineTask<int> stopped() {
	co_await ex::just_stopped();
	co_return 0;
}

/// Sets \p ran once it runs: a task with nothing to report but having run.
InlineTask<void> mark(std::atomic<bool> &ran) {
	ran = true;
	co_return;
}
} // namespace

SCENARIO("An InlineTask resumes where the awaited sender completed",
		 "[task]") {
	GIVEN("a thread other than the caller's") {
		exec::single_thread_context other;

		WHEN("a task awaits a sender that completes there") {
			auto result =
				ex::sync_wait(thread_after_hop(other.get_scheduler()));

			THEN("it carries on there, without hopping back") {
				// A task with scheduler affinity would be back on the
				// sync_wait thread. This is what InlineTaskEnv is for, and
				// what a wrong environment type (one without
				// start_scheduler_type) silently loses.
				REQUIRE(result.has_value());
				CHECK(std::get<0>(*result) == other.get_thread_id());
				CHECK(std::get<0>(*result) != std::this_thread::get_id());
			}
		}
	}
}

SCENARIO("An InlineTask is a sender", "[task]") {
	WHEN("it co_returns a value") {
		auto result = ex::sync_wait(answer());

		THEN("the value is what it completes with") {
			REQUIRE(result.has_value());
			CHECK(std::get<0>(*result) == 42);
		}
	}

	WHEN("it throws") {
		THEN("it completes with the error") {
			CHECK_THROWS_AS(ex::sync_wait(fail()), std::runtime_error);
		}
	}

	WHEN("an awaited sender is stopped") {
		auto result = ex::sync_wait(stopped());

		THEN("the task completes as stopped") { CHECK(!result.has_value()); }
	}

	WHEN("it is spawned, which offers it no scheduler to start on") {
		std::atomic<bool> ran = false;
		ex::simple_counting_scope scope;
		// spawn() takes only senders that cannot fail, and a task can: handle
		// its error first, as the server does with its own tasks.
		ex::spawn(supervised<FailPolicy::Isolate>(
					  mark(ran), [](const std::exception_ptr &) noexcept {}),
				  scope.get_token());
		ex::sync_wait(scope.join());

		THEN("it runs") { CHECK(ran); }
	}
}
