#pragma once

#include <chrono>

namespace redis {

/**
 * \brief A monotonic clock cheap enough to read twice per command.
 *
 * For timing short spans on the hot path (per-command stats). On macOS,
 * std::chrono::steady_clock goes through clock_gettime and
 * mach_continuous_time, which also counts time asleep and costs several times
 * a plain counter read; this reads mach_absolute_time instead. Elsewhere it is
 * steady_clock, which is already cheap (vDSO on Linux, QPC on Windows).
 *
 * Do not use it for deadlines or anything that must keep counting while the
 * machine sleeps: on macOS it stops then.
 */
struct TickClock {
	using duration   = std::chrono::nanoseconds;
	using rep        = duration::rep;
	using period     = duration::period;
	using time_point = std::chrono::time_point<TickClock>;

	static constexpr bool is_steady = true;

	[[nodiscard]] static time_point now() noexcept;
};

} // namespace redis
