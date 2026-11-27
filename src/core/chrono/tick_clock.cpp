// TickClock where steady_clock is already a cheap counter read: the vDSO on
// Linux, QPC on Windows. macOS has its own, os/tick_clock_mach.cpp; CMake
// builds one or the other.
#include "tick_clock.hpp"

namespace redis {

// steady_clock::now() reads a counter and takes no lock, but the library
// cannot say so.
REDIS_TRUSTED_NONBLOCKING_BEGIN
TickClock::time_point TickClock::now() noexcept REDIS_NONBLOCKING {
	return time_point(std::chrono::duration_cast<duration>(
		std::chrono::steady_clock::now().time_since_epoch()));
}
REDIS_TRUSTED_NONBLOCKING_END

} // namespace redis
