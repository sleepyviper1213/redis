// TickClock on macOS (see tick_clock.hpp): mach_absolute_time, which
// steady_clock does not use. The other platforms build chrono/tick_clock.cpp.
#include "core/chrono/tick_clock.hpp"

#include <mach/mach_time.h>

namespace redis {
namespace {
// Ticks to nanoseconds: 1/1 on Intel, 125/3 on Apple silicon. Read once, not
// on every call as clock_gettime does.
const mach_timebase_info_data_t timebase = [] {
	mach_timebase_info_data_t info{};
	mach_timebase_info(&info);
	return info;
}();
} // namespace

// mach_absolute_time() reads a counter from the commpage: no lock, no syscall,
// but the SDK header cannot say so.
REDIS_TRUSTED_NONBLOCKING_BEGIN
TickClock::time_point TickClock::now() noexcept REDIS_NONBLOCKING {
	// No overflow for about a century of uptime at 24 MHz ticks.
	const auto ticks = static_cast<rep>(mach_absolute_time());
	return time_point(duration(ticks * timebase.numer / timebase.denom));
}
REDIS_TRUSTED_NONBLOCKING_END

} // namespace redis
