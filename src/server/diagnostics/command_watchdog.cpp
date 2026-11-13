#include "command_watchdog.hpp"

#include "server/log_channels.hpp"

#include <utility>

namespace redis::diagnostics {

std::optional<Stall> StallDetector::check(TickClock::time_point now) noexcept {
	const auto beat = heartbeat();
	if (!beat) return std::nullopt;
	if (policy_ == ReportPolicy::Once && beat->generation == reported_)
		return std::nullopt;
	const auto running = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - beat->started);
	if (running < threshold_) return std::nullopt;
	reported_ = beat->generation;
	return Stall{.command = beat->command, .running = running};
}

CommandWatchdog::CommandWatchdog(ReportPolicy policy,
								 CommandWatchdogSettings settings,
								 Report report)
	: settings_(settings),
	  report_(std::move(report)),
	  detector_(policy, settings.threshold) {
	if (settings_.threshold.count() > 0) context_.emplace();
}

// The scheduler's clock times the wait only; how long the command has run is
// judged on TickClock, the clock it was stamped with.
void CommandWatchdog::look() noexcept {
	if (const auto stall = detector_.check(TickClock::now())) report_(*stall);
}

void CommandWatchdog::log_stall(const Stall &stall) {
	logging::logger_for(logging::channel::server)
		.warn("Command '{}' ({} args) has been running for {} ms; no other "
			  "command runs until it ends",
			  stall.command.name,
			  stall.command.argc,
			  stall.running.count());
}

[[nodiscard]] bool CommandWatchdog::is_enabled() const noexcept {
	return context_.has_value();
}
} // namespace redis::diagnostics
