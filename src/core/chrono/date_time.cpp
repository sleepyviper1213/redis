#include "date_time.hpp"

#include "core/utils/saturating.hpp"

#include <atomic>
#include <limits>

namespace redis {
namespace {
constexpr int64_t MS_PER_S = 1000;

// The seam's state (DX-90). The time is stored before the source is switched
// (release) and read after it (acquire), so a reader that sees Fixed sees its
// time. On x86 and ARM64 the acquire load is a plain load.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<WallClockSource> current_source{WallClockSource::System};
std::atomic<int64_t> fixed_ms{0};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

DateTime DateTime::current() noexcept {
	if (current_source.load(std::memory_order_acquire) ==
		WallClockSource::Fixed) [[unlikely]]
		return DateTime(fixed_ms.load(std::memory_order_relaxed));
	return from_std_time_point(std::chrono::system_clock::now());
}

void DateTime::use_source(WallClockSource source, DateTime fixed_at) noexcept {
	fixed_ms.store(fixed_at.ms_, std::memory_order_relaxed);
	current_source.store(source, std::memory_order_release);
}

DateTime DateTime::from_msecs_since_epoch(int64_t ms) noexcept {
	return DateTime(ms);
}

DateTime DateTime::from_secs_since_epoch(int64_t secs) noexcept {
	return DateTime(mul_sat(secs, MS_PER_S));
}

DateTime DateTime::distant_past() noexcept {
	return DateTime(std::numeric_limits<int64_t>::min());
}

DateTime DateTime::distant_future() noexcept {
	return DateTime(std::numeric_limits<int64_t>::max());
}

[[nodiscard]] int64_t DateTime::to_msecs_since_epoch() const noexcept {
	return ms_;
}


int64_t DateTime::to_secs_since_epoch() const noexcept {
	const int64_t secs = ms_ / MS_PER_S;
	return (ms_ % MS_PER_S < 0) ? secs - 1 : secs;
}

[[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds>
DateTime::to_std_sys_milliseconds() const noexcept {
	return std::chrono::sys_time<std::chrono::milliseconds>(
		std::chrono::milliseconds(ms_));
}


DateTime DateTime::add_msecs(int64_t ms) const noexcept {
	return DateTime(add_sat(ms_, ms));
}

DateTime DateTime::add_secs(int64_t secs) const noexcept {
	return add_msecs(mul_sat(secs, MS_PER_S));
}

int64_t DateTime::msecs_to(DateTime other) const noexcept {
	return sub_sat(other.ms_, ms_);
}

} // namespace redis
