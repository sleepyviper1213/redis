#pragma once

#include "core/chrono/tick_clock.hpp"
#include "core/stdexec_ext/every.hpp"
#include "core/stdexec_ext/watchdog.hpp"
#include "core/utils/attributes.hpp"
#include "current_command.hpp"

#include <exec/timed_thread_scheduler.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <chrono>
#include <cstdint>
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

/// A command that has been running longer than the threshold.
struct Stall {
	CurrentCommand command;
	std::chrono::milliseconds running;
};

/**
 * \brief Decides, from the heartbeat, whether the running command has
 * stalled, and whether to report it again under its ReportPolicy.
 *
 * Separate from CommandWatchdog so it can be tested without a thread or
 * real time: check() takes the time to judge by. Used by one thread at a time.
 */
class StallDetector {
public:
	StallDetector(ReportPolicy policy,
				  std::chrono::milliseconds threshold) noexcept
		: policy_(policy), threshold_(threshold) {}

	/// The running command, if it has run \p threshold by \p now and the
	/// policy lets it be reported (again).
	[[nodiscard]] std::optional<Stall>
	check(TickClock::time_point now) noexcept;

private:
	ReportPolicy policy_;
	std::chrono::milliseconds threshold_;
	/// The generation last reported. Generations start at 1.
	std::uint64_t reported_ = 0;
};

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
 * the rest of its long-running tasks.
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
	 * The CommandWatchdog must outlive the sender, and run it once at a time:
	 * the detector it judges with is its own.
	 *
	 * \return A sender; its type is deduced. Enabled, it completes only as
	 *         stopped, and on the watchdog's own thread (see every()), so
	 *         whoever must carry on elsewhere hops back with continues_on().
	 */
	[[nodiscard]] stdexec::sender auto run() REDIS_LIFETIMEBOUND {
		// Named once, so the type below and the sender returned agree: every
		// lambda expression is a type of its own. A pointer, not the detector:
		// every() copies it each round, and the detector must remember what it
		// reported.
		const auto look = [this]() noexcept { this->look(); };
		using Watching  = decltype(stdexec_ext::every(
			std::declval<exec::timed_thread_scheduler>(),
			settings_.period,
			look));
		using Skip      = decltype(stdexec::just());
		using Result    = exec::variant_sender<Watching, Skip>;

		if (!context_) return Result(stdexec::just());
		return Result(stdexec_ext::every(context_->get_scheduler(),
										 settings_.period,
										 look));
	}

	/// The default Report: a warning on the server channel.
	static void log_stall(const Stall &stall);

private:
	/// One look: what run() calls every period.
	void look() noexcept;

	CommandWatchdogSettings settings_;
	Report report_;
	StallDetector detector_;
	/// The thread run() waits and looks on; only with a threshold. Last, so
	/// it is stopped and joined before what it uses goes away.
	std::optional<exec::timed_thread_context> context_;
};

} // namespace redis::diagnostics
