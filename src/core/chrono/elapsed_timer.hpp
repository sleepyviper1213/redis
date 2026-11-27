#pragma once

#include "clock.hpp"

#include <fmt/base.h>

#include <chrono>
#include <cstdint>

namespace redis {
/**
 * \brief Measures time elapsed since a start point.
 *
 * API modelled after QElapsedTimer; the counterpart of BasicDeadlineTimer,
 * which is when something must be done by, on the same choice of clock
 * (ADR-0013).
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
	has_expired(std::chrono::duration<double> timeout) const noexcept {
		return duration_elapsed() >= timeout;
	}

private:
	Clock::time_point start_;
};

using ElapsedTimer = BasicElapsedTimer<>;
} // namespace redis

template <typename T>
struct fmt::formatter<redis::BasicElapsedTimer<T>> : fmt::formatter<double> {
	auto format(const redis::BasicElapsedTimer<T> &sw,
				format_context &ctx) const -> format_context::iterator {
		return formatter<double>::format(sw.duration_elapsed().count(), ctx);
	}
};