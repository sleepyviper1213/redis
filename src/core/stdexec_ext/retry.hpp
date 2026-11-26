#pragma once

#include "core/stdexec_ext/every.hpp"

#include <exec/repeat_until.hpp>
#include <exec/timed_scheduler.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <exception>
#include <optional>
#include <random>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

namespace redis::stdexec_ext {

namespace detail {
/// An attempt's values, decayed, kept until the retry completes.
/// \tparam Ts The types one set_value signature of the attempt sends.
template <class... Ts>
using Values = std::tuple<std::decay_t<Ts>...>;

/**
 * The one tuple of values an attempt can complete with. An attempt that only
 * ever fails has none, and one with several set_value signatures cannot be
 * retried into a single result.
 *
 * \tparam Tuples One Values<...> per set_value signature of the attempt.
 */
template <class... Tuples>
struct OnlyImpl {
	static_assert(sizeof...(Tuples) <= 1,
				  "retry: the attempt has several set_value signatures");
	using type = std::tuple<>;
};

/// The single set_value signature: its tuple is the answer.
/// \tparam Tuple The attempt's one Values<...>.
template <class Tuple>
struct OnlyImpl<Tuple> {
	using type = Tuple;
};

/// The tuple of values the attempt completes with; see OnlyImpl.
/// \tparam Tuples One Values<...> per set_value signature of the attempt.
template <class... Tuples>
using Only = OnlyImpl<Tuples...>::type;

/**
 * Whatever an attempt failed with, as an exception_ptr: that is what a
 * RetryFunc and the retry's own error channel speak.
 * \tparam Error What the attempt's set_error was called with: an
 *         exception_ptr (kept as is), an error_code (wrapped in a
 *         system_error), or any other type (stored as the exception itself).
 */
template <class Error>
std::exception_ptr as_exception(Error &&error) {
	using E = std::decay_t<Error>;
	if constexpr (std::is_same_v<E, std::exception_ptr>)
		return std::forward<Error>(error);
	else if constexpr (std::is_same_v<E, std::error_code>)
		return std::make_exception_ptr(std::system_error(error));
	else return std::make_exception_ptr(std::forward<Error>(error));
}

/// A sender completing with the values in \p values, moved out of the tuple.
/// \tparam Ts The types held by the tuple, and so the types the sender sends.
template <class... Ts>
stdexec::sender auto just_values(std::tuple<Ts...> &&values) {
	return std::apply(
		[]<class... V>(V &&...v) {
			return stdexec::just(std::forward<V>(v)...);
		},
		std::move(values));
}
} // namespace detail

/**
 * What retry() takes to build one attempt: callable as const, giving a
 * sender.
 * \tparam F The function's type, usually a lambda.
 */
template <class F>
concept AttemptFunc = std::invocable<const F &> &&
					  stdexec::sender<std::invoke_result_t<const F &>>;

/**
 * What retry() takes to decide after a failure: called with the number of
 * failures so far and the error, giving a sender of bool (true: try again).
 * \tparam F The function's type, usually a lambda.
 */
template <class F>
concept RetryFunc =
	std::invocable<const F &, size_t, std::exception_ptr> &&
	stdexec::sender<
		std::invoke_result_t<const F &, size_t, std::exception_ptr>>;

/**
 * \brief Runs a sender again each time it fails, until it succeeds or
 * \p should_retry says to give up.
 *
 * \p make_attempt builds one attempt: a sender completing with at most one
 * set_value signature (an attempt that needs an environment to say what it
 * completes with is not supported). It is called afresh for every attempt,
 * which is what lets the attempt hold state that must not outlive it, and
 * what repeat_until needs: it restarts one sender, so building it runs only
 * once.
 *
 * \p should_retry is called after each failed attempt with the number of
 * failures so far (1 for the first) and the error as an exception_ptr. It
 * returns a sender completing with a bool: true to try again, false to give
 * up. That one hook is where logging, backoff delays and attempt limits live;
 * see retry_policy for ready-made ones. A stop request while it runs ends the
 * retry as stopped.
 *
 * Completes with the first successful attempt's values. If \p should_retry
 * gives up it completes with the last error, always as an exception_ptr. A
 * stop request, during an attempt or while \p should_retry runs, completes as
 * stopped and is never retried.
 *
 * A sender, not a coroutine, although it is a loop: it is generic library
 * code, which the caller runs on whatever scheduler and in whatever context
 * it likes, and it allocates nothing. Runs as a loop (repeat_until), so a
 * stream of attempts that fail at once does not deepen the stack.
 *
 * \p make_attempt and \p should_retry are moved into the returned sender.
 *
 * \tparam MakeAttempt The type of the function that builds the sender to run,
 *         afresh for every attempt (an AttemptFunc).
 * \tparam ShouldRetry The type of the function that decides, after each
 *         failure, whether to try again (a RetryFunc).
 * \return A sender; its exact type is deduced, and depends on both parameters
 *         through what the attempt sends.
 */
template <AttemptFunc MakeAttempt, RetryFunc ShouldRetry>
stdexec::sender auto retry(MakeAttempt make_attempt, ShouldRetry should_retry) {
	using AttemptSender = std::invoke_result_t<const MakeAttempt &>;
	using Result        = stdexec::value_types_of_t<AttemptSender,
													stdexec::env<>,
													detail::Values,
													detail::Only>;

	// What survives from one attempt to the next, in the operation state.
	struct State {
		size_t failures = 0;
		std::optional<Result> result;
		std::exception_ptr error; ///< Set once should_retry gave up.
	};

	return stdexec::just(State{}) |
		   stdexec::let_value([make_attempt = std::move(make_attempt),
							   should_retry =
								   std::move(should_retry)](State &state) {
			   stdexec::sender auto attempt =
				   stdexec::just() | stdexec::let_value([&make_attempt] {
					   return make_attempt();
				   }) |
				   stdexec::then([&state]<class... Values>(Values &&...values) {
					   state.result.emplace(std::forward<Values>(values)...);
					   return true; // done
				   }) |
				   stdexec::let_error(
					   [&state, &should_retry]<class Error>(Error &&error) {
						   const std::exception_ptr failure =
							   detail::as_exception(std::forward<Error>(error));
						   return should_retry(++state.failures, failure) |
								  stdexec::then([&state, failure](bool again) {
									  if (!again) state.error = failure;
									  return !again; // done when giving up
								  });
					   });

			   using Failed =
				   decltype(stdexec::just_error(std::exception_ptr{}));
			   using Done =
				   decltype(detail::just_values(std::declval<Result>()));
			   using Outcome = exec::variant_sender<Failed, Done>;

			   return exec::repeat_until(std::move(attempt)) |
					  stdexec::let_value([&state]() -> Outcome {
						  if (state.error)
							  return Outcome(stdexec::just_error(state.error));
						  return Outcome(
							  detail::just_values(std::move(*state.result)));
					  });
		   });
}

namespace detail {
/// What a backoff wait completes with: try again. A named type, so that the
/// sender built in exponential_backoff() and the one named in its variant are
/// the same type.
struct TryAgain {
	bool operator()() const noexcept { return true; }
};
} // namespace detail

/**
 * \brief Ready-made RetryFuncs for retry(): which to pass says when to try
 * again, as in `retry(attempt, retry_policy::immediately(3))`.
 *
 * retry() is the one algorithm; these are only its commonest policies. One
 * that must look at the error or log it (the accept loop's) is a lambda of
 * its own, which can still take its delay from backoff_delay().
 */
namespace retry_policy {

/// Try again straight away, up to \p max_attempts attempts in all. Its type
/// is deduced.
[[nodiscard]] inline RetryFunc auto immediately(size_t max_attempts) {
	return [max_attempts](size_t failures, const std::exception_ptr &) {
		return stdexec::just(failures < max_attempts);
	};
}

/**
 * The wait after failure \p failures (1 for the first): \p initial, doubled
 * for each failure after the first, never past \p cap. It stops doubling at
 * the cap, so no count of failures overflows it.
 * \tparam Duration A std::chrono::duration; \p cap converts to it, so
 *         `backoff_delay(100ms, 5s, n)` is in milliseconds.
 */
template <class Duration>
[[nodiscard]] constexpr Duration
backoff_delay(Duration initial, std::type_identity_t<Duration> cap,
			  size_t failures) noexcept {
	if (initial <= Duration::zero()) return Duration::zero();
	Duration delay = std::min(initial, cap);
	for (size_t i = 1; i < failures && delay < cap; ++i)
		delay = delay > cap / 2 ? cap : delay * 2;
	return delay;
}

/**
 * What exponential_backoff() takes to spread its waits: called with the
 * backoff delay, giving the wait to use instead, from zero to that delay.
 * \tparam J The jitter's type: full_jitter(), NoJitter, or a lambda.
 * \tparam Duration The waits' type.
 */
template <class J, class Duration>
concept Jitter = std::invocable<const J &, Duration> &&
				 std::convertible_to<std::invoke_result_t<const J &, Duration>,
									 Duration>;

/// The jitter that does nothing: every wait is the whole backoff delay.
struct NoJitter {
	template <class Duration>
	constexpr Duration operator()(Duration upper) const noexcept {
		return upper;
	}
};

/**
 * Full jitter: each wait drawn uniformly from zero to the backoff delay, so
 * that clients which failed together do not retry together.
 *
 * Owns its engine: a policy is copied into the retry that uses it, and an
 * engine held by reference would have to outlive every one. Drawing changes
 * the engine, which is why it is mutable; one retry asks its policy once at
 * a time, so it needs no lock, but one FullJitter must not be shared between
 * retries running at once.
 *
 * \tparam Engine A uniform random bit generator, std::mt19937_64 say.
 */
template <std::uniform_random_bit_generator Engine>
class FullJitter {
public:
	explicit FullJitter(Engine engine) : engine_(std::move(engine)) {}

	template <class Duration>
	Duration operator()(Duration upper) const {
		if (upper <= Duration::zero()) return Duration::zero();
		std::uniform_int_distribution<typename Duration::rep> draw(
			0, upper.count());
		return Duration(draw(engine_));
	}

private:
	mutable Engine engine_;
};

/// Full jitter over \p engine (see FullJitter), as exponential_backoff()'s
/// last argument: `full_jitter(std::mt19937_64{seed})`.
template <std::uniform_random_bit_generator Engine>
[[nodiscard]] FullJitter<Engine> full_jitter(Engine engine) {
	return FullJitter<Engine>(std::move(engine));
}

/**
 * The policy exponential_backoff() returns: a RetryFunc, named rather than a
 * lambda because it holds four things and builds a variant.
 */
template <class Scheduler, class Duration, class J>
struct ExponentialBackoff {
	Scheduler scheduler;
	size_t max_attempts;
	Duration initial;
	Duration cap;
	J jitter;

	auto operator()(size_t failures, const std::exception_ptr &) const {
		using GiveUp = decltype(stdexec::just(false));
		using Wait   = decltype(exec::schedule_after(scheduler, initial) |
                              stdexec::then(detail::TryAgain{}));
		using Next   = exec::variant_sender<GiveUp, Wait>;

		if (failures >= max_attempts) return Next(stdexec::just(false));
		const Duration wait = jitter(backoff_delay(initial, cap, failures));
		return Next(exec::schedule_after(scheduler, wait) |
					stdexec::then(detail::TryAgain{}));
	}
};

/**
 * After failure n, wait backoff_delay(\p initial, \p cap, n) on \p scheduler,
 * or what \p jitter makes of it, and try again, up to \p max_attempts attempts
 * in all. Giving up does not wait. A stop request during the wait ends the
 * retry as stopped (see retry()).
 *
 * `exponential_backoff(timer, 4, 100ms, 5s)`: four attempts, waiting 100,
 * 200 and 400 ms between them.
 *
 * \tparam Scheduler A TimedScheduler, copied into the policy.
 * \tparam Duration The waits' type, one the scheduler takes; deduced from
 *         \p initial, and \p cap converts to it.
 * \tparam J A Jitter: NoJitter (the default) or full_jitter().
 */
template <class Scheduler, class Duration, class J = NoJitter>
	requires TimedScheduler<Scheduler, Duration> && Jitter<J, Duration>
[[nodiscard]] RetryFunc auto
exponential_backoff(Scheduler scheduler, size_t max_attempts, Duration initial,
					std::type_identity_t<Duration> cap, J jitter = {}) {
	return ExponentialBackoff<Scheduler, Duration, J>{.scheduler = std::move(scheduler),
													  .max_attempts = max_attempts,
													  .initial = initial,
													  .cap     = cap,
													  .jitter  = std::move(jitter)};
}

} // namespace retry_policy

} // namespace redis::stdexec_ext
