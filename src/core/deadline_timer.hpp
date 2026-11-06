#pragma once

#include "utils/attributes.hpp"

#include <chrono>
#include <cstdint>

namespace redis {

/**
 * \brief A wall-clock deadline, as Redis key expiry requires: EXPIREAT, PXAT
 * and snapshot files all exchange absolute Unix times, so system_clock is the
 * only correct choice and is not a policy.
 */
class DeadlineTimer {
public:
	using clock      = std::chrono::system_clock;
	using time_point = clock::time_point;
	using duration   = clock::duration;

	static DeadlineTimer at(time_point deadline) noexcept;

	static DeadlineTimer after(duration from_now) noexcept;

	[[nodiscard]] REDIS_PURE_FN time_point deadline() const noexcept;

	[[nodiscard]] bool has_expired() const noexcept;

	/// Remaining time, clamped to zero when the deadline has passed.
	[[nodiscard]] duration remaining_time() const noexcept;

	/// Remaining time in whole milliseconds, ceiled, clamped to zero.
	[[nodiscard]] std::chrono::milliseconds remaining_time_ms() const noexcept;

	[[nodiscard]] int64_t remaining_time_nsecs() const noexcept;

	/// The deadline as milliseconds since the Unix epoch, for wire protocols
	/// and snapshot files that exchange absolute POSIX timestamps.
	[[nodiscard]] REDIS_PURE_FN int64_t millis_since_epoch() const noexcept;

	/// Construct a deadline \p secs / \p millis after now (EXPIRE, PEXPIRE).
	[[nodiscard]] static DeadlineTimer after_s(int64_t secs) noexcept;
	[[nodiscard]] static DeadlineTimer after_ms(int64_t millis) noexcept;

	/// Construct a deadline at an absolute Unix timestamp (EXPIREAT, PEXPIREAT).
	[[nodiscard]] REDIS_CONST_FN static DeadlineTimer at_s(int64_t unix_secs) noexcept;
	[[nodiscard]] REDIS_CONST_FN static DeadlineTimer at_ms(int64_t unix_ms) noexcept;

	[[nodiscard]] static time_point now() noexcept;

	friend auto operator<=>(const DeadlineTimer &,
							const DeadlineTimer &) = default;

protected:
	explicit DeadlineTimer(time_point deadline) noexcept;

private:
	time_point deadline_;
};

} // namespace redis
