#pragma once
#include <chrono>

namespace redis {
using timespan_t   = std::chrono::system_clock::duration;
using timestamp_t = std::chrono::system_clock::time_point;
} // namespace redis
