#pragma once

#include "elapsed_timer.hpp"
#include "tick_clock.hpp"

namespace redis {
/// For spans timed on the hot path, such as once per command: on macOS,
/// steady_clock costs about a tenth of a pipelined SET; see TickClock.
using TickTimer = BasicElapsedTimer<TickClock>;

} // namespace redis