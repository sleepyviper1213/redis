#pragma once

#include <cstdint>

namespace redis::stdexec_ext {

/**
 * How often one piece of work that has run too long is reported. There is no
 * default: every watch names it, so the call site says which.
 */
enum class ReportPolicy : std::uint8_t {
	/// One report, when the work first passes the threshold.
	Once,
	/// A report every threshold (or every look, for a heartbeat monitor) for
	/// as long as the work keeps running, as Redis' watchdog does.
	EveryPeriod,
};

} // namespace redis::stdexec_ext
