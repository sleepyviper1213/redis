#pragma once

#include "core/chrono/clock.hpp"
#include "core/error/verify.hpp"

#include <atomic>
#include <chrono>

namespace redis::test {

/**
 * \brief A monotonic std::chrono clock that moves only when a test says so
 * (ADR-0013, TC-328).
 *
 * For any Clock template parameter: BasicElapsedTimer, BasicDeadlineTimer,
 * HeartbeatSlot. Starts at zero; advance() is the only way time passes, so a
 * timeout fires because the test said so, not because the machine was slow.
 *
 * Each \p Tag is a clock of its own, so a test can run two independent ones.
 * The state is per process: call reset() at the start of a test that needs a
 * known origin. Under CTest each scenario runs in its own process anyway.
 *
 * The wall clock behind DeadlineTimer is simulated by SimulatedWallClock
 * instead (DX-90): its users are not templates.
 */
template <class Tag = void>
struct SimulatedClock {
	using duration   = std::chrono::nanoseconds;
	using rep        = duration::rep;
	using period     = duration::period;
	using time_point = std::chrono::time_point<SimulatedClock>;

	static constexpr bool is_steady = true;

	[[nodiscard]] static time_point now() noexcept {
		return time_point(duration(ticks.load(std::memory_order_acquire)));
	}

	/// Moves the clock forward by \p by. Going back would break is_steady.
	static void advance(duration by) noexcept {
		REDIS_VERIFY(by >= duration::zero(), "a monotonic clock never goes back");
		ticks.fetch_add(by.count(), std::memory_order_acq_rel);
	}

	/// Back to the origin, between tests.
	static void reset() noexcept { ticks.store(0, std::memory_order_release); }

private:
	static inline std::atomic<rep> ticks{0};
};

static_assert(MonotonicClock<SimulatedClock<>>);

} // namespace redis::test
