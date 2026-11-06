#include "deadline_timer.hpp"

namespace redis {

DeadlineTimer::DeadlineTimer(time_point deadline) noexcept
	: deadline_(deadline) {}

[[nodiscard]] DeadlineTimer DeadlineTimer::at(time_point deadline) noexcept {
	return DeadlineTimer(deadline);
}

[[nodiscard]] DeadlineTimer DeadlineTimer::after(duration from_now) noexcept {
	return DeadlineTimer(now() + from_now);
}

[[nodiscard]] DeadlineTimer::time_point
DeadlineTimer::deadline() const noexcept {
	return deadline_;
}

[[nodiscard]] bool DeadlineTimer::has_expired() const noexcept {
	return clock::now() >= deadline_;
}

[[nodiscard]] DeadlineTimer::duration
DeadlineTimer::remaining_time() const noexcept {
	const auto left = deadline_ - clock::now();
	return left > duration::zero() ? left : duration::zero();
}

[[nodiscard]] std::chrono::milliseconds
DeadlineTimer::remaining_time_ms() const noexcept {
	return std::chrono::ceil<std::chrono::milliseconds>(remaining_time());
}

[[nodiscard]] int64_t DeadlineTimer::remaining_time_nsecs() const noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(remaining_time())
		.count();
}

[[nodiscard]] int64_t DeadlineTimer::millis_since_epoch() const noexcept {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   deadline_.time_since_epoch())
		.count();
}

[[nodiscard]] DeadlineTimer DeadlineTimer::after_s(int64_t secs) noexcept {
	return after(std::chrono::seconds(secs));
}

[[nodiscard]] DeadlineTimer DeadlineTimer::after_ms(int64_t millis) noexcept {
	return after(std::chrono::milliseconds(millis));
}

[[nodiscard]] DeadlineTimer DeadlineTimer::at_s(int64_t unix_secs) noexcept {
	return at(time_point(std::chrono::seconds(unix_secs)));
}

[[nodiscard]] DeadlineTimer DeadlineTimer::at_ms(int64_t unix_ms) noexcept {
	return at(time_point(std::chrono::milliseconds(unix_ms)));
}

[[nodiscard]] DeadlineTimer::time_point DeadlineTimer::now() noexcept {
	return clock::now();
}

} // namespace redis
