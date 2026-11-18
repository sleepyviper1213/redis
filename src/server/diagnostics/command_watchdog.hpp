#pragma once

#include "core/chrono/tick_clock.hpp"
#include "core/stdexec_ext/watchdog/heartbeat.hpp"
#include "core/stdexec_ext/watchdog/policy.hpp"
#include "core/utils/attributes.hpp"
#include "current_command.hpp"

#include <exec/timed_thread_scheduler.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <chrono>
#include <functional>
#include <optional>

namespace redis::diagnostics {

/// How often one stuck command is reported; named by whoever makes a
/// StallDetector or CommandWatchdog.
using stdexec_ext::ReportPolicy;

struct CommandWatchdogSettings {
	static constexpr std::chrono::milliseconds DEFAULT_THRESHOLD{1000};
	static constexpr std::chrono::milliseconds DEFAULT_PERIOD{100};

	/// Report a command that has run this long. Zero: no watchdog.
	std::chrono::milliseconds threshold = DEFAULT_THRESHOLD;
	/// How often to look: how late after the threshold a report can come.
	std::chrono::milliseconds period = DEFAULT_PERIOD;
};

/// A command that has been running longer than the threshold: which, and
/// for how long.
using Stall = stdexec_ext::Stall<CurrentCommand, TickClock>;

/// Decides, from command_heartbeat(), whether the running command has
/// stalled; what CommandWatchdog judges with.
using StallDetector = stdexec_ext::StallDetector<CurrentCommand, TickClock>;

/**
 * \brief Reports a command running too long on the keyspace thread, while
 * that thread is still stuck in it: nothing else is served meanwhile.
 *
 * It looks from a thread of its own (a timed_thread_context), not from a
 * timer on the event loop: with one I/O thread, such a timer would wait
 * behind the very command it should report. It only reports; the command is
 * never interrupted, since a running command has no point at which a stop
 * request could reach it. How long the command ran in all is in the
 * per-command stats once it ends.
 *
 * run() is the work, as a sender, so the server starts and stops it with
 * the rest of its long-running tasks: stdexec_ext::monitor() over
 * command_heartbeat().
 */
class CommandWatchdog {
public:
	using Report = std::function<void(const Stall &)>;

	/// \param policy How often one stuck command is reported.
	/// \param report Called on the watchdog's thread with each stall. Must
	///        not throw: the process ends if it does. Logs a warning by
	///        default.
	CommandWatchdog(ReportPolicy policy, CommandWatchdogSettings settings,
					Report report = log_stall);

	CommandWatchdog(const CommandWatchdog &)            = delete;
	CommandWatchdog &operator=(const CommandWatchdog &) = delete;
	CommandWatchdog(CommandWatchdog &&)                 = delete;
	CommandWatchdog &operator=(CommandWatchdog &&)      = delete;
	~CommandWatchdog()                                  = default;

	/// Whether run() watches anything: a threshold is set.
	[[nodiscard]] bool is_enabled() const noexcept;

	/**
	 * \brief Look every period until stopped. Without a threshold, it
	 * completes at once instead, having nothing to do.
	 *
	 * The CommandWatchdog must outlive the sender.
	 *
	 * \return A sender; its type is deduced. Enabled, it completes only as
	 *         stopped, and on the watchdog's own thread (see monitor()), so
	 *         whoever must carry on elsewhere hops back with continues_on().
	 */
	[[nodiscard]] stdexec::sender auto run() REDIS_LIFETIMEBOUND {
		// Named once, so the type below and the sender returned agree: every
		// lambda expression is a type of its own.
		const auto report = [this](const Stall &stall) noexcept {
			report_(stall);
		};
		const TickClock::duration threshold = settings_.threshold;
		using Watching                      = decltype(stdexec_ext::monitor(
			command_heartbeat(),
			std::declval<exec::timed_thread_scheduler>(),
			settings_.period,
			policy_,
			threshold,
			report));
		using Skip                          = decltype(stdexec::just());
		using Result = exec::variant_sender<Watching, Skip>;

		if (!context_) return Result(stdexec::just());
		return Result(stdexec_ext::monitor(command_heartbeat(),
										   context_->get_scheduler(),
										   settings_.period,
										   policy_,
										   threshold,
										   report));
	}

	/// The default Report: a warning on the server channel.
	static void log_stall(const Stall &stall);

private:
	CommandWatchdogSettings settings_;
	ReportPolicy policy_;
	Report report_;
	/// The thread run() waits and looks on; only with a threshold. Last, so
	/// it is stopped and joined before what it uses goes away.
	std::optional<exec::timed_thread_context> context_;
};

} // namespace redis::diagnostics
