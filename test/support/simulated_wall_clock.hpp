#pragma once

#include "core/chrono/date_time.hpp"

#include <chrono>

namespace redis::test {

/**
 * \brief Fixes the wall clock for the scope of a test (DX-90, #53).
 *
 * While it lives, DateTime::current() (and with it every deadline, TTL and
 * snapshot timestamp) reads this clock; advance() is the only way time
 * passes. A deadline then passes because the test said so, not because the
 * machine was slow, and no test needs to sleep. The destructor gives the
 * system clock back, so a simulated clock never leaks into the next test.
 *
 * One at a time: they are not nested.
 */
class SimulatedWallClock {
public:
	/// Starts at \p start; by default the system time when it is created, so
	/// absolute times (EXPIREAT, snapshot files) stay realistic.
	explicit SimulatedWallClock(DateTime start = DateTime::current()) noexcept
		: now_(start) {
		DateTime::use_source(WallClockSource::Fixed, now_);
	}

	~SimulatedWallClock() { DateTime::use_source(WallClockSource::System, {}); }

	SimulatedWallClock(const SimulatedWallClock &)            = delete;
	SimulatedWallClock &operator=(const SimulatedWallClock &) = delete;
	SimulatedWallClock(SimulatedWallClock &&)                 = delete;
	SimulatedWallClock &operator=(SimulatedWallClock &&)      = delete;

	/// Moves the clock forward by \p by.
	void advance(std::chrono::milliseconds by) noexcept {
		now_ = now_.add_msecs(by.count());
		DateTime::use_source(WallClockSource::Fixed, now_);
	}

	[[nodiscard]] DateTime now() const noexcept { return now_; }

	/// now() in milliseconds since the Unix epoch, for PXAT and PEXPIREAT.
	[[nodiscard]] int64_t now_ms() const noexcept {
		return now_.to_msecs_since_epoch();
	}

private:
	DateTime now_;
};

} // namespace redis::test
