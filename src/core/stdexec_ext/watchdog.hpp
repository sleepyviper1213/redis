#pragma once

#include "core/utils/attributes.hpp"
#include "every.hpp"

#include <exec/finally.hpp>
#include <exec/timed_scheduler.hpp>
#include <stdexec/execution.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace redis::stdexec_ext {

/**
 * How often one piece of work that has run too long is reported. There is no
 * default: every watchdog names it, so the call site says which.
 */
enum class ReportPolicy : std::uint8_t {
	/// One report, when the work first passes the threshold.
	Once,
	/// A report every threshold for as long as the work keeps running, as
	/// Redis' watchdog does.
	EveryPeriod,
};

namespace detail {
/// How long work has run, on \p Scheduler's clock: what on_stall is told.
template <class Scheduler>
using Elapsed = decltype(exec::now(std::declval<const Scheduler &>()) -
						 exec::now(std::declval<const Scheduler &>()));

/**
 * What one watchdog run shares between its two branches: the monitor and
 * the work's completion, which stops it. Kept in let_value's operation state,
 * so its address is stable while both run.
 *
 * It is moved only before the watchdog starts, with the sender that holds
 * it: the move makes a fresh stop source rather than copying one (stop
 * sources cannot move), which is right because nothing can have used it yet.
 */
template <class Scheduler, class StallFunc>
struct WatchState {
	Scheduler scheduler;
	StallFunc on_stall;
	exec::time_point_of_t<Scheduler> started{};
	stdexec::inplace_stop_source monitor_stop;

	WatchState(Scheduler s, StallFunc f)
		: scheduler(std::move(s)), on_stall(std::move(f)) {}

	WatchState(WatchState &&other) noexcept
		: scheduler(std::move(other.scheduler)),
		  on_stall(std::move(other.on_stall)),
		  started(other.started) {}

	WatchState(const WatchState &)            = delete;
	WatchState &operator=(const WatchState &) = delete;
	WatchState &operator=(WatchState &&)      = delete;
	~WatchState()                             = default;
};

/**
 * The reports under \p Policy: one once \p threshold has passed, or one
 * every \p threshold. They run on the scheduler's thread, and end only when
 * stopped.
 */
template <ReportPolicy Policy, class State, class Duration>
stdexec::sender auto reports(State &state REDIS_LIFETIMEBOUND,
							 Duration threshold) {
	// By reference to the state: every() copies the report each round.
	const auto report = [&state]() noexcept {
		state.on_stall(exec::now(state.scheduler) - state.started);
	};
	if constexpr (Policy == ReportPolicy::Once)
		return exec::schedule_after(state.scheduler, threshold) |
			   stdexec::then(report);
	else return every(state.scheduler, threshold, report);
}

/**
 * The branch of watchdog() that watches: reports() until
 * state.monitor_stop is requested.
 *
 * It answers to that stop source, not to the caller's stop token: it must
 * end when the work does, however the work ends. Being stopped is its normal
 * end, so it then completes with nothing rather than stopping when_all.
 */
template <ReportPolicy Policy, class State, class Duration>
stdexec::sender auto monitor(State &state REDIS_LIFETIMEBOUND,
							 Duration threshold) {
	return reports<Policy>(state, threshold) |
		   stdexec::write_env(stdexec::prop{stdexec::get_stop_token,
											state.monitor_stop.get_token()}) |
		   stdexec::upon_stopped([]() noexcept {});
}

/// The branch of watchdog() that works: \p work, then the monitor stopped,
/// on a value, an error or a stop alike (finally).
template <class Work, class State>
stdexec::sender auto stopping_monitor(Work work,
									  State &state REDIS_LIFETIMEBOUND) {
	return std::move(work) |
		   exec::finally(stdexec::just() | stdexec::then([&state]() noexcept {
							 state.monitor_stop.request_stop();
						 }));
}

/**
 * What let_value runs as a watchdog starts, given its state: both branches,
 * joined by when_all.
 *
 * \tparam Policy, Work, Duration As watchdog()'s.
 */
template <ReportPolicy Policy, class Work, class Duration>
struct StartWatch {
	Work work;
	Duration threshold;

	/// Not const: the work is moved out, as it is started only once.
	template <class State>
	stdexec::sender auto operator()(State &state REDIS_LIFETIMEBOUND) {
		// Here, not when the sender was built: this runs as it starts.
		state.started = exec::now(state.scheduler);
		return stdexec::when_all(stopping_monitor(std::move(work), state),
								 monitor<Policy>(state, threshold));
	}
};
} // namespace detail

/**
 * \brief \p work, reported to \p on_stall if it is still running after
 * \p threshold, without being interrupted.
 *
 * The monitoring counterpart of a timeout: a timeout cancels work that
 * waits too long, a watchdog only says that the work is taking long. That is
 * the one thing that can be done about work which cannot be cancelled, such
 * as a loop that never reaches a point where a stop request could end it.
 *
 * Completes as \p work does, with its values, error or stop, once the timer
 * has been cancelled too: \p work that finishes before \p threshold costs a
 * timer armed and cancelled, and is never reported. A stop request reaches
 * \p work as it would without the watchdog.
 *
 * \p on_stall runs on \p scheduler's thread while \p work is still running
 * elsewhere, so whatever it touches must be safe to touch from there. Under
 * ReportPolicy::Once it is called at most once; under EveryPeriod, every
 * \p threshold until \p work completes.
 *
 * Built from when_all, which already waits for both branches before it
 * completes, so the timer is never left running when the operation state
 * goes away. \p work must therefore have at most one set_value signature, as
 * when_all needs, and its values are decay-copied once, as when_all does.
 *
 * \tparam Policy How often a stall is reported; see ReportPolicy.
 * \tparam Work The type of the watched sender.
 * \tparam Scheduler A TimedScheduler with a thread of its own, such as
 *         exec::timed_thread_scheduler. It must not be the one \p work runs
 *         on: the report could not come until the work let it run.
 * \tparam Duration The threshold's type, one the scheduler takes.
 * \tparam StallFunc What is told how long \p work has run, as the scheduler's
 *         duration; it must not throw.
 * \return A sender; its type is deduced.
 */
template <ReportPolicy Policy, stdexec::sender Work, class Scheduler,
		  class Duration, class StallFunc>
	requires TimedScheduler<Scheduler, Duration> &&
			 std::is_nothrow_invocable_v<StallFunc &,
										 detail::Elapsed<Scheduler>>
stdexec::sender auto watchdog(Work work, Scheduler scheduler,
							  Duration threshold, StallFunc on_stall) {
	using State = detail::WatchState<Scheduler, StallFunc>;

	return stdexec::just(State(std::move(scheduler), std::move(on_stall))) |
		   stdexec::let_value(detail::StartWatch<Policy, Work, Duration>{
			   .work      = std::move(work),
			   .threshold = threshold});
}

} // namespace redis::stdexec_ext
