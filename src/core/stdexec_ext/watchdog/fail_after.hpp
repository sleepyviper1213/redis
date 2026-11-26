#pragma once

#include "core/stdexec_ext/every.hpp"
#include "core/stdexec_ext/pipeable.hpp"

#include <exec/timed_scheduler.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <concepts>
#include <system_error>
#include <utility>

namespace redis::stdexec_ext {

namespace detail {
/// What the timer side of fail_after() completes with once the limit passes.
struct TimedOut {
	auto operator()() const noexcept {
		return stdexec::just_error(std::make_error_code(std::errc::timed_out));
	}
};

/// fail_after()'s on_expiry when there is nothing to force.
struct NothingToForce {
	void operator()() const noexcept {}
};
} // namespace detail

/**
 * \brief \p work, failed with `std::errc::timed_out` if \p limit passes
 * first: whichever finishes first cancels the other through the stop token,
 * and the race completes once both have unwound.
 *
 * time_limited() replaces a late answer with a value; this makes it a
 * failure, so that retry() sees a hang as it sees any other failed attempt
 * (a std::system_error holding `timed_out`, to its policy) and tries again.
 * The error is an error code, not an exception: nothing is thrown. \p work's
 * own values and errors pass through unchanged.
 *
 * For a retried operation, the limit goes inside the attempt, so that each
 * attempt gets its own: `retry([&] { return request() | fail_after(timer,
 * 2s); }, policy)`. Around the whole retry() it would bound every attempt
 * together, and a timeout would end the retry rather than be retried.
 *
 * No attempt outlives its race: when_any waits for \p work to unwind once it
 * is stopped. That needs \p work to end promptly when asked. Where a stop
 * request alone cannot promise that (a send that Windows does not complete
 * while the peer is not reading, say), \p on_expiry forces it, by closing
 * the socket for example: it runs as soon as the limit passes, before the
 * race waits for \p work.
 *
 * The limit is timed from when the race starts (see time_limited()).
 *
 * \tparam Work The type of the raced sender.
 * \tparam Scheduler A TimedScheduler; \p on_expiry runs on it.
 * \tparam Duration The limit's type, one the scheduler takes.
 * \tparam OnExpiry What runs when the time runs out: no arguments, returning
 *         nothing.
 * \return A sender; its type is deduced.
 */
template <stdexec::sender Work, class Scheduler, class Duration,
		  std::invocable OnExpiry = detail::NothingToForce>
	requires TimedScheduler<Scheduler, Duration> &&
			 std::same_as<std::invoke_result_t<OnExpiry>, void>
stdexec::sender auto fail_after(Work work, Scheduler scheduler, Duration limit,
								OnExpiry on_expiry = {}) {
	return exec::when_any(
		std::move(work),
		exec::schedule_after(std::move(scheduler), std::move(limit)) |
			stdexec::then(std::move(on_expiry)) |
			stdexec::let_value(detail::TimedOut{}));
}

/// `work | fail_after(scheduler, limit[, on_expiry])`: as above.
template <class Scheduler, class Duration,
		  std::invocable OnExpiry = detail::NothingToForce>
	requires TimedScheduler<Scheduler, Duration> &&
			 std::same_as<std::invoke_result_t<OnExpiry>, void>
auto fail_after(Scheduler scheduler, Duration limit, OnExpiry on_expiry = {}) {
	return Pipeable{
		[scheduler = std::move(scheduler),
		 limit,
		 on_expiry = std::move(on_expiry)]<class Work>(Work &&work) mutable {
			return fail_after(std::forward<Work>(work),
							  std::move(scheduler),
							  limit,
							  std::move(on_expiry));
		}};
}

} // namespace redis::stdexec_ext
