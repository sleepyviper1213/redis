#include "deadline_timer.hpp"

#include "../utils/saturating.hpp"

#include <cstdint>

namespace redis {

DeadlineTimer::DeadlineTimer(DateTime deadline) noexcept
	: deadline_(deadline) {}

[[nodiscard]] DeadlineTimer DeadlineTimer::at(DateTime deadline) noexcept {
	return DeadlineTimer(deadline);
}

[[nodiscard]] DeadlineTimer DeadlineTimer::after(duration from_now) noexcept {
	return DeadlineTimer(DateTime::current().add_msecs(from_now.count()));
}

[[nodiscard]] DateTime DeadlineTimer::deadline() const noexcept {
	return deadline_;
}

[[nodiscard]] bool DeadlineTimer::has_expired() const noexcept {
	return DateTime::current() >= deadline_;
}

[[nodiscard]] DeadlineTimer::duration
DeadlineTimer::remaining_time() const noexcept {
	const auto left = duration(DateTime::current().msecs_to(deadline_));
	return left > duration::zero() ? left : duration::zero();
}

[[nodiscard]] std::chrono::milliseconds
DeadlineTimer::remaining_time_ms() const noexcept {
	return remaining_time();
}

[[nodiscard]] int64_t DeadlineTimer::millis_since_epoch() const noexcept {
	return deadline_.to_msecs_since_epoch();
}

[[nodiscard]] DeadlineTimer DeadlineTimer::forever() noexcept {
	return DeadlineTimer(DateTime::distant_future());
}

[[nodiscard]] bool DeadlineTimer::is_forever() const noexcept {
	return deadline_ == DateTime::distant_future();
}

[[nodiscard]] DeadlineTimer DeadlineTimer::after_s(int64_t secs) noexcept {
	constexpr int64_t MS_PER_S = 1000;
	return after(duration(mul_sat(secs, MS_PER_S)));
}

[[nodiscard]] DeadlineTimer DeadlineTimer::after_ms(int64_t millis) noexcept {
	return after(duration(millis));
}

[[nodiscard]] DeadlineTimer DeadlineTimer::at_s(int64_t unix_secs) noexcept {
	return at(DateTime::from_secs_since_epoch(unix_secs));
}

[[nodiscard]] DeadlineTimer DeadlineTimer::at_ms(int64_t unix_ms) noexcept {
	return at(DateTime::from_msecs_since_epoch(unix_ms));
}

} // namespace redis
