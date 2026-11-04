#pragma once

#include <cstddef>
#include <string_view>

// What the server was doing, for a crash report to say.
//
// A crash is far easier to explain with the command that triggered it, so
// the dispatcher records each command's name before running it and clears it
// after. Recording costs a few stores, so it is always on.
namespace redis::diagnostics {

/**
 * \brief Record \p name (with \p argc arguments, the name included) as the
 * command being executed. Names longer than the record keeps are truncated.
 *
 * Commands run one at a time (see Executor), so there is one writer. The
 * reader is a crash handler on any thread, which may see a half-written
 * record: acceptable for a report written while the process dies.
 */
void set_current_command(std::string_view name, std::size_t argc) noexcept;

/// No command is being executed.
void clear_current_command() noexcept;

/// What a crash report reads: the name is empty when no command is running.
struct CurrentCommand {
	std::string_view name;
	std::size_t argc = 0;
};

/// The record as it is now. Async-signal-safe: a few loads, no allocation.
[[nodiscard]] CurrentCommand current_command() noexcept;

/// Sets the current command for the scope, clearing it on every exit path.
class CommandScope {
public:
	CommandScope(std::string_view name, std::size_t argc) noexcept {
		set_current_command(name, argc);
	}
	~CommandScope() { clear_current_command(); }

	CommandScope(const CommandScope &)            = delete;
	CommandScope &operator=(const CommandScope &) = delete;
	CommandScope(CommandScope &&)                 = delete;
	CommandScope &operator=(CommandScope &&)      = delete;
};

} // namespace redis::diagnostics
