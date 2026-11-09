#pragma once

#include <chrono>
#include <cstdint>

namespace redis {

/**
 * \brief Measures time elapsed since a start point.
 *
 * API modelled after QElapsedTimer. Distinct from DeadlineTimer, which tracks
 * a fixed future wall-clock deadline; ElapsedTimer uses the monotonic clock
 * and grows forward from a chosen start point.
 */
class ElapsedTimer {
public:
	using clock    = std::chrono::steady_clock;
	using Duration = clock::duration;

	/// Starts the timer now.
	ElapsedTimer() noexcept;

	/// Restarts the timer from now.
	void start() noexcept;

	/// Restarts the timer and returns the milliseconds elapsed since the
	/// previous start (like QElapsedTimer::restart()).
	[[nodiscard]] std::chrono::milliseconds restart() noexcept;

	[[nodiscard]] Duration duration_elapsed() const noexcept;

	[[nodiscard]] std::chrono::milliseconds elapsed() const noexcept;

	[[nodiscard]] int64_t nsecs_elapsed() const noexcept;

	/// True if more than \p timeout has elapsed since start or last restart().
	[[nodiscard]] bool has_expired(std::chrono::milliseconds timeout) const noexcept;

private:
	clock::time_point start_;
};

} // namespace redis
