#pragma once

#include "core/stdexec_ext/pipeable.hpp"
#include "policy.hpp"
#include "time_limited.hpp"
#include "watched.hpp"

#include <stdexec/execution.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace redis::stdexec_ext {

/// The two limits of escalate(): when work is reported, and when it is given
/// up on.
template <class Duration>
struct Escalation {
	/// Report the work once it has run this long (watched()).
	Duration report_after;
	/// Cancel it once it has run this long (time_limited()). Longer than
	/// report_after, or there is never anything to report.
	Duration cancel_after;
};

/**
 * \brief \p work, reported to \p on_stall once it has run
 * \p limits.report_after, and abandoned at \p limits.cancel_after: a warning
 * first, then the time limit.
 *
 * The two other members of the watchdog vocabulary composed (see
 * watchdog.hpp): `time_limited(watched(work, ...), ...)`, each with its own
 * rules. As with watched(), \p on_stall runs on \p scheduler's thread while
 * \p work runs elsewhere, under \p Policy; as with time_limited(), \p on_expiry
 * returns what \p work sends and runs as soon as the hard limit passes, and
 * the race ends once \p work has unwound.
 *
 * \tparam Policy How often a stall is reported before the time limit.
 * \tparam Scheduler A TimedScheduler with a clock and a thread of its own (as
 *         watched() needs), on which both limits are timed.
 * \return A sender; its type is deduced.
 */
template <ReportPolicy Policy, stdexec::sender Work, class Scheduler,
		  class Duration, class StallFunc, std::invocable OnExpiry>
	requires TimedScheduler<Scheduler, Duration> &&
			 std::is_nothrow_invocable_v<StallFunc &,
										 detail::Elapsed<Scheduler>>
stdexec::sender auto escalate(Work work, Scheduler scheduler,
							  Escalation<Duration> limits, StallFunc on_stall,
							  OnExpiry on_expiry) {
	return time_limited(watched<Policy>(std::move(work),
										scheduler,
										limits.report_after,
										std::move(on_stall)),
						std::move(scheduler),
						limits.cancel_after,
						std::move(on_expiry));
}

/// `work | escalate<Policy>(scheduler, limits, on_stall, on_expiry)`: as
/// above.
template <ReportPolicy Policy, class Scheduler, class Duration, class StallFunc,
		  std::invocable OnExpiry>
	requires TimedScheduler<Scheduler, Duration> &&
			 std::is_nothrow_invocable_v<StallFunc &,
										 detail::Elapsed<Scheduler>>
auto escalate(Scheduler scheduler, Escalation<Duration> limits,
			  StallFunc on_stall, OnExpiry on_expiry) {
	return Pipeable{
		[scheduler = std::move(scheduler),
		 limits,
		 on_stall  = std::move(on_stall),
		 on_expiry = std::move(on_expiry)]<class Work>(Work &&work) mutable {
			return escalate<Policy>(std::forward<Work>(work),
									std::move(scheduler),
									limits,
									std::move(on_stall),
									std::move(on_expiry));
		}};
}

} // namespace redis::stdexec_ext
