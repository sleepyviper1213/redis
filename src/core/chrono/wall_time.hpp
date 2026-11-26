#pragma once

#include "date_time.hpp"

#include <chrono>

namespace redis {

/**
 * \brief The wall clock as a std::chrono clock (ADR-0013, DX-91).
 *
 * Its time points are std::chrono::sys_time in milliseconds: POSIX time, the
 * same 64-bit milliseconds since the Unix epoch as DateTime, and what Redis
 * clients and files exchange (EXPIREAT, PXAT, snapshot deadlines). Standard
 * code reads them directly, and clock_cast converts them.
 *
 * now() reads DateTime::current(), so a test's SimulatedWallClock applies
 * (DX-90). Not monotonic: it follows the system time when it is set.
 */
struct WallTime {
	using duration   = std::chrono::milliseconds;
	using rep        = duration::rep;
	using period     = duration::period;
	using time_point = std::chrono::sys_time<duration>;

	static constexpr bool is_steady = false;

	[[nodiscard]] static time_point now() noexcept {
		return DateTime::current().to_std_sys_milliseconds();
	}
};

} // namespace redis
