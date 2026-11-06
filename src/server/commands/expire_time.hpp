#pragma once
#include "core/date_time.hpp"
#include "core/deadline_timer.hpp"
#include "core/utils/saturating.hpp"

#include <cstdint>
#include <optional>

namespace redis {

/// Whether an expiry argument counts seconds or milliseconds, and from when.
enum class ExpireUnit : bool { Seconds, Milliseconds };
enum class ExpireBase : bool { Relative, Absolute };

/**
 * \brief The deadline an EXPIRE / SET expiry argument names, or std::nullopt if
 * the arithmetic overflows - Redis's "invalid expire time".
 *
 * Redis's rule: seconds are scaled to milliseconds, and a relative amount is
 * added to the current time, both without overflowing an int64. (DeadlineTimer
 * itself saturates; the commands reject instead, so a client is told rather
 * than silently given a key that never expires.)
 */
[[nodiscard]] inline std::optional<DeadlineTimer>
expire_time(int64_t amount, ExpireUnit unit, ExpireBase base) noexcept {
	constexpr int64_t MS_PER_S = 1000;
	int64_t millis             = amount;
	if (unit == ExpireUnit::Seconds && mul_overflow(amount, MS_PER_S, &millis))
		return std::nullopt;
	if (base == ExpireBase::Relative &&
		add_overflow(millis,
					 DateTime::current().to_msecs_since_epoch(),
					 &millis))
		return std::nullopt;
	return DeadlineTimer::at_ms(millis);
}

} // namespace redis
