#pragma once

#include "date_time.hpp"
#include "core/utils/attributes.hpp"

#include <chrono>
#include <cstdint>

namespace redis {

/**
 * \brief A wall-clock deadline, as Redis key expiry requires: EXPIREAT, PXAT
 * and snapshot files all exchange absolute Unix times, so the DateTime wall
 * clock is the only correct choice and is not a policy.
 *
 * An overflowing deadline becomes forever() (or the distant past) instead of
 * wrapping.
 */
class DeadlineTimer {
public:
	using duration = std::chrono::milliseconds;

	static DeadlineTimer at(DateTime deadline) noexcept;

	static DeadlineTimer after(duration from_now) noexcept;

	[[nodiscard]] REDIS_PURE_FN DateTime deadline() const noexcept;

	[[nodiscard]] bool has_expired() const noexcept;

	/// Remaining time, clamped to zero when the deadline has passed.
	[[nodiscard]] duration remaining_time() const noexcept;

	[[nodiscard]] std::chrono::milliseconds remaining_time_ms() const noexcept;

	/// The deadline as milliseconds since the Unix epoch, for wire protocols
	/// and snapshot files that exchange absolute POSIX timestamps.
	[[nodiscard]] REDIS_PURE_FN int64_t millis_since_epoch() const noexcept;

	/// Construct a deadline \p secs / \p millis after now (EXPIRE, PEXPIRE).
	[[nodiscard]] static DeadlineTimer after_s(int64_t secs) noexcept;
	[[nodiscard]] static DeadlineTimer after_ms(int64_t millis) noexcept;

	/// Construct a deadline at an absolute Unix timestamp (EXPIREAT,
	/// PEXPIREAT).
	[[nodiscard]] REDIS_CONST_FN static DeadlineTimer
	at_s(int64_t unix_secs) noexcept;
	[[nodiscard]] REDIS_CONST_FN static DeadlineTimer
	at_ms(int64_t unix_ms) noexcept;

	/// A deadline that never passes.
	[[nodiscard]] static DeadlineTimer forever() noexcept;
	[[nodiscard]] bool is_forever() const noexcept;

	friend auto operator<=>(const DeadlineTimer &,
							const DeadlineTimer &) = default;

protected:
	explicit DeadlineTimer(DateTime deadline) noexcept;

private:
	DateTime deadline_;
};

} // namespace redis
