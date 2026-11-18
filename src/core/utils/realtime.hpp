#pragma once

#include "core/utils/attributes.hpp"

#include <utility>

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#include <sanitizer/rtsan_interface.h>
#define REDIS_HAS_RTSAN 1
#endif
#endif

namespace redis {

/**
 * \brief Lets the code in scope do what a REDIS_NONBLOCKING region may not
 * (allocate, free), for the rare slow path inside one: a vector that outgrew
 * its reserve, an error reply being formatted. Under RealtimeSanitizer the
 * checks are off while it lives; elsewhere it does nothing.
 */
class [[maybe_unused]] RealtimeExemption {
public:
	RealtimeExemption()                                     = default;
	RealtimeExemption(const RealtimeExemption &)            = delete;
	RealtimeExemption &operator=(const RealtimeExemption &) = delete;

private:
#ifdef REDIS_HAS_RTSAN
	__rtsan::ScopedDisabler disabler_;
#endif
};

/// v.push_back(x), exempting the allocation when \p v is full. A reserve that
/// is big enough makes the exemption dead code; a request that outgrows it
/// still works, and RealtimeSanitizer stays quiet about that one slow step.
template <typename Vector, typename T>
void push_back_growing(Vector &v, T &&x) {
	if (v.size() == v.capacity()) {
		[[maybe_unused]] const RealtimeExemption growing;
		v.push_back(std::forward<T>(x));
		return;
	}
	v.push_back(std::forward<T>(x));
}

} // namespace redis
