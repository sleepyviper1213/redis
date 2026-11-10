#include "tick_clock.hpp"

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

namespace redis {

#ifdef __APPLE__
namespace {
// Ticks to nanoseconds: 1/1 on Intel, 125/3 on Apple silicon. Read once, not
// on every call as clock_gettime does.
const mach_timebase_info_data_t timebase = [] {
	mach_timebase_info_data_t info{};
	mach_timebase_info(&info);
	return info;
}();
} // namespace

TickClock::time_point TickClock::now() noexcept {
	// No overflow for about a century of uptime at 24 MHz ticks.
	const auto ticks = static_cast<rep>(mach_absolute_time());
	return time_point(duration(ticks * timebase.numer / timebase.denom));
}
#else
TickClock::time_point TickClock::now() noexcept {
	return time_point(std::chrono::duration_cast<duration>(
		std::chrono::steady_clock::now().time_since_epoch()));
}
#endif

} // namespace redis
