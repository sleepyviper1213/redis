#include "core/logging/guard.hpp"

#include "core/logging/lifecycle.hpp"

namespace redis::logging {

guard::guard(const settings &config) { init(config); }

// shutdown, not flush: under settings::async an async flush is only posted to
// the queue, and the worker thread must be joined here rather than during
// static destruction. @see logging::shutdown
guard::~guard() { shutdown(); }

} // namespace redis::logging
