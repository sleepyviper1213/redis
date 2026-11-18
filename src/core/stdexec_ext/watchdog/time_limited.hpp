#pragma once

#include "core/stdexec_ext/every.hpp"
#include "core/stdexec_ext/pipeable.hpp"

#include <exec/timed_scheduler.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <concepts>
#include <utility>

namespace redis::stdexec_ext {

/**
 * \brief \p work, abandoned if \p limit passes first: whichever finishes
 * first cancels the other through the stop token, and the race completes
 * once both have unwound, with the winner's result.
 *
 * The cancelling member of the watchdog vocabulary (see watchdog.hpp); what
 * watched() only reports, this ends.
 *
 * Out of time, it completes with what \p on_expiry returns, so \p on_expiry
 * returns a value of the type \p work sends (or nothing, if \p work sends
 * nothing): both branches then complete the same way, and the caller tells
 * them apart by that value.
 *
 * \p on_expiry runs as soon as the limit passes, before the race waits for
 * \p work to unwind, which needs \p work to finish promptly once stopped.
 * Where a stop request alone cannot promise that (an operating system call
 * that a cancellation does not complete, say), \p on_expiry is where to force
 * it, and where to count the expiry at the time it happens.
 *
 * The limit is timed from when the race starts, not from when the sender
 * was built, provided \p scheduler's schedule_after reads the clock as it
 * starts (timed_thread_scheduler and TimerScheduler do), so one sender
 * restarted every round by repeat_until gets the whole limit each round.
 *
 * \tparam Work The type of the raced sender: one set_value signature, as
 *         when_any needs of both branches alike.
 * \tparam Scheduler A TimedScheduler; \p on_expiry runs on it.
 * \tparam Duration The limit's type, one the scheduler takes.
 * \tparam OnExpiry What runs when the time runs out: no arguments, returning
 *         what \p work sends.
 * \return A sender; its type is deduced.
 */
template <stdexec::sender Work, class Scheduler, class Duration,
		  std::invocable OnExpiry>
	requires TimedScheduler<Scheduler, Duration>
stdexec::sender auto time_limited(Work work, Scheduler scheduler,
								  Duration limit, OnExpiry on_expiry) {
	return exec::when_any(
		std::move(work),
		exec::schedule_after(std::move(scheduler), std::move(limit)) |
			stdexec::then(std::move(on_expiry)));
}

/// `work | time_limited(scheduler, limit, on_expiry)`: as above.
template <class Scheduler, class Duration, std::invocable OnExpiry>
	requires TimedScheduler<Scheduler, Duration>
auto time_limited(Scheduler scheduler, Duration limit, OnExpiry on_expiry) {
	return Pipeable{
		[scheduler = std::move(scheduler),
		 limit,
		 on_expiry = std::move(on_expiry)]<class Work>(Work &&work) mutable {
			return time_limited(std::forward<Work>(work),
								std::move(scheduler),
								limit,
								std::move(on_expiry));
		}};
}

} // namespace redis::stdexec_ext
