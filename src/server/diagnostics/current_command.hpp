#pragma once

#include "core/chrono/tick_clock.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// What the server was doing, for a crash report or the watchdog to say.
//
// A crash is far easier to explain with the command that triggered it, so
// the dispatcher records each command's name before running it and clears it
// after. Recording costs a few stores, so it is always on.
namespace redis::diagnostics {

/**
 * \brief Record \p name (with \p argc arguments, the name included) as the
 * command being executed since \p started. Names longer than the record keeps
 * are truncated.
 *
 * Commands run one at a time (see Executor), so there is one writer. Two
 * kinds of reader may run on any thread: a crash handler, which may see a
 * half-written record (acceptable for a report written while the process
 * dies), and the watchdog; see heartbeat().
 *
 * \pre \p name stays valid until the next call: the watchdog keeps a pointer
 *      to it, not a copy. CommandInfo names are string literals.
 */
void set_current_command(std::string_view name, std::size_t argc,
						 TickClock::time_point started) noexcept;

/// No command is being executed.
void clear_current_command() noexcept;

/// What a crash report reads: the name is empty when no command is running.
struct CurrentCommand {
	std::string_view name;
	std::size_t argc = 0;
};

/// The record as it is now. Async-signal-safe: a few loads, no allocation.
[[nodiscard]] CurrentCommand current_command() noexcept;

/// The command running now and since when: what the watchdog reads.
struct Heartbeat {
	/// One more for every command started, so two reads of one long
	/// command can be told from two commands that each look long.
	std::uint64_t generation = 0;
	TickClock::time_point started;
	CurrentCommand command;
};

/**
 * \brief The command running now, or nullopt when none is. Unlike
 * current_command(), safe to call from any thread at any time: every field it
 * reads is atomic.
 *
 * The fields all come from one command: a read that a new command overlaps
 * gives nullopt rather than a mix of the two. A watchdog loses nothing by it:
 * that command has only just started.
 */
[[nodiscard]] std::optional<Heartbeat> heartbeat() noexcept;

/// Sets the current command for the scope, clearing it on every exit path.
class CommandScope {
public:
	CommandScope(std::string_view name, std::size_t argc,
				 TickClock::time_point started) noexcept {
		set_current_command(name, argc, started);
	}

	~CommandScope() { clear_current_command(); }

	CommandScope(const CommandScope &)            = delete;
	CommandScope &operator=(const CommandScope &) = delete;
	CommandScope(CommandScope &&)                 = delete;
	CommandScope &operator=(CommandScope &&)      = delete;
};

} // namespace redis::diagnostics
