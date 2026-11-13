#pragma once

#include <exec/repeat_until.hpp>
#include <exec/timed_scheduler.hpp>
#include <stdexec/execution.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace redis::stdexec_ext {

/**
 * What every() runs on: a scheduler that can wait for a duration. Spells out
 * what exec::schedule_after needs, which stdexec only names internally.
 * \tparam S The scheduler's type.
 * \tparam Duration The type of the period.
 */
template <class S, class Duration>
concept TimedScheduler =
	stdexec::scheduler<S> && requires(const S &s, const Duration &d) {
		{ exec::schedule_after(s, d) } -> stdexec::sender;
	};

/**
 * \brief Calls \p step on \p scheduler every \p period, until stopped.
 *
 * Fixed delay, not fixed rate: the next wait starts once \p step returns, so a
 * slow step delays the next one and never overlaps it. The first call comes
 * one period after the sender starts, not at once.
 *
 * The loop's own work is to watch, not to be watched: a stop request ends it
 * during a wait, and it completes as stopped. That is the only way it ends,
 * so it fits where a loop must run as long as something else does, such as a
 * sibling in exec::when_any or stdexec::when_all.
 *
 * \p step is copied every round, as repeat_until restarts a copy of one
 * sender, so state that must outlive a round lives behind a pointer or
 * reference it captures.
 *
 * Each wait is timed from when it starts, not from when the sender was
 * built: schedule_after on a scheduler with only schedule_at reads now() as
 * it starts, and repeat_until starts a fresh copy each round.
 *
 * \tparam Scheduler A TimedScheduler, e.g. exec::timed_thread_scheduler.
 *         \p step runs on its thread.
 * \tparam Duration The period's type, one the scheduler takes.
 * \tparam Step What is called every period: no arguments, returning nothing,
 *         and not throwing.
 * \return A sender; its type is deduced. It completes only as stopped.
 */
template <class Scheduler, class Duration, std::invocable Step>
	requires TimedScheduler<Scheduler, Duration> &&
			 std::is_nothrow_invocable_r_v<void, Step &>
stdexec::sender auto every(Scheduler scheduler, Duration period, Step step) {
	return exec::schedule_after(std::move(scheduler), std::move(period)) |
		   stdexec::then([step = std::move(step)]() mutable noexcept {
			   step();
			   return false; // never done: only a stop request ends the loop
		   }) |
		   exec::repeat_until();
}

} // namespace redis::stdexec_ext
