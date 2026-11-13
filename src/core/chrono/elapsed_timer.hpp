#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>

namespace redis {
/// A clock that never goes back: what an elapsed time must be measured on.
/// Spells out std::chrono::is_clock_v, which libc++ does not ship yet.
template <class C>
concept MonotonicClock = requires {
	typename C::rep;
	typename C::period;
	typename C::duration;
	typename C::time_point;
	{ C::now() } -> std::same_as<typename C::time_point>;
} && C::is_steady;

/**
 * \brief Measures time elapsed since a start point.
 *
 * API modelled after QElapsedTimer. Distinct from DeadlineTimer, which tracks
 * a fixed future wall-clock deadline; ElapsedTimer uses a monotonic clock
 * and grows forward from a chosen start point.
 *
 * \tparam Clock A steady clock. steady_clock by default; TickClock for short
 * spans on the hot path, where steady_clock is costly on macOS.
 */
template <MonotonicClock Clock = std::chrono::steady_clock>
class BasicElapsedTimer {
public:
	using clock    = Clock;
	using Duration = Clock::duration;

	/// Starts the timer now.
	BasicElapsedTimer() noexcept : start_(Clock::now()) {}

	/// Restarts the timer from now.
	void start() noexcept { start_ = Clock::now(); }

	/// Restarts the timer and returns the milliseconds elapsed since the
	/// previous start (like QElapsedTimer::restart()).
	[[nodiscard]] std::chrono::milliseconds restart() noexcept {
		const auto now = Clock::now();
		const auto ms =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - start_);
		start_ = now;
		return ms;
	}

	/// When the timer was last started.
	[[nodiscard]] Clock::time_point started() const noexcept { return start_; }

	[[nodiscard]] Duration duration_elapsed() const noexcept {
		return Clock::now() - start_;
	}

	[[nodiscard]] std::chrono::milliseconds elapsed() const noexcept {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			duration_elapsed());
	}

	[[nodiscard]] int64_t nsecs_elapsed() const noexcept {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(
				   duration_elapsed())
			.count();
	}

	/// True if more than \p timeout has elapsed since start or last restart().
	[[nodiscard]] bool
	has_expired(std::chrono::milliseconds timeout) const noexcept {
		return elapsed() >= timeout;
	}

private:
	Clock::time_point start_;
};

using ElapsedTimer = BasicElapsedTimer<>;


} // namespace redis
