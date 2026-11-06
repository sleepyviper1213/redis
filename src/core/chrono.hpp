#pragma once

#include "utils/attributes.hpp"

#include <chrono>
#include <cstdint>

namespace redis::chrono {

using clock      = std::chrono::system_clock;
using time_point = clock::time_point;
using duration   = clock::duration;

/// Unix timestamp in whole seconds.
[[nodiscard]] REDIS_CONST_FN inline int64_t to_unix_s(time_point tp) noexcept {
	return std::chrono::duration_cast<std::chrono::seconds>(
			   tp.time_since_epoch())
		.count();
}

/// Unix timestamp in milliseconds.
[[nodiscard]] REDIS_CONST_FN inline int64_t to_unix_ms(time_point tp) noexcept {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   tp.time_since_epoch())
		.count();
}

} // namespace redis::chrono
