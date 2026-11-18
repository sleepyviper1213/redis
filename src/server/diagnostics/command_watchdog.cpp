#include "command_watchdog.hpp"

#include "server/log_channels.hpp"

#include <chrono>
#include <utility>

namespace redis::diagnostics {

CommandWatchdog::CommandWatchdog(ReportPolicy policy,
								 CommandWatchdogSettings settings,
								 Report report)
	: settings_(settings), policy_(policy), report_(std::move(report)) {
	if (settings_.threshold.count() > 0) context_.emplace();
}

void CommandWatchdog::log_stall(const Stall &stall) {
	logging::logger_for(logging::channel::server)
		.warn(
			"Command '{}' ({} args) has been running for {} ms; no other "
			"command runs until it ends",
			stall.subject.name,
			stall.subject.argc,
			std::chrono::duration_cast<std::chrono::milliseconds>(stall.running)
				.count());
}

[[nodiscard]] bool CommandWatchdog::is_enabled() const noexcept {
	return context_.has_value();
}
} // namespace redis::diagnostics
