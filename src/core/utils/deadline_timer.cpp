#include "core/utils/deadline_timer.hpp"

#include <chrono>

namespace redis {
DeadlineTimer::DeadlineTimer(timespan_type remaining)
	: expire_time_(remaining.count() >= 0
					   ? timepoint_type(clock_type::now() + remaining)
					   : timepoint_type::max()) {}

DeadlineTimer::DeadlineTimer(timepoint_type deadline)
	: expire_time_(deadline) {}

DeadlineTimer DeadlineTimer::forever() noexcept {
	return DeadlineTimer(timepoint_type::max());
}

bool DeadlineTimer::has_expired() const noexcept {
	if (is_forever()) return false;
	return clock_type::now() >= expire_time_;
}

bool DeadlineTimer::is_forever() const noexcept {
	return expire_time_ == timepoint_type::max();
}

DeadlineTimer::timespan_type DeadlineTimer::time_left() const noexcept {
	if (is_forever()) return timespan_type(-1); // forever
	auto now = clock_type::now();
	return (expire_time_ > now) ? (expire_time_ - now) : timespan_type::zero();
}

int64_t DeadlineTimer::millis_left() const noexcept {
	return std::chrono::duration_cast<std::chrono::milliseconds>(time_left())
		.count();
}

int64_t DeadlineTimer::seconds_left() const noexcept {
	return std::chrono::duration_cast<std::chrono::seconds>(time_left())
		.count();
}

const DeadlineTimer::timepoint_type &DeadlineTimer::deadline() const noexcept {
	return expire_time_;
}

DeadlineTimer::DeadlineTimer() : expire_time_(clock_type::now()) {}

void DeadlineTimer::expire_after(timespan_type ttl) noexcept {
	expire_time_ = clock_type::now() + ttl;
}

void DeadlineTimer::expire_after(int64_t secs, int64_t millisecs) noexcept {
	expire_time_ = clock_type::now() + std::chrono::seconds(secs) +
				   std::chrono::milliseconds(millisecs);
}

void DeadlineTimer::expire_after(timepoint_type start,
								 timespan_type ttl) noexcept {
	expire_time_ = start + ttl;
}

void DeadlineTimer::expire_at(timepoint_type tp) noexcept { expire_time_ = tp; }

void DeadlineTimer::expire_at_milli(int64_t msSinceEpoch) noexcept {
	expire_time_ =
		clock_type::time_point{std::chrono::milliseconds(msSinceEpoch)};
}

void DeadlineTimer::expire_at_sec(int64_t sSinceEpoch) noexcept {
	expire_time_ = clock_type::time_point(std::chrono::seconds(sSinceEpoch));
}
} // namespace redis
