#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

// The only POSIX-specific code in the server: process management for
// background saves, and fsync for durable snapshot writes.
namespace redis::persistence::posix {

/// Process id of a child created by fork_and_run().
using pid_type = int;

/**
 * \brief fork() the process. The child runs \p task, then exits immediately
 * with status 0 if it returned true and 1 otherwise, without running
 * destructors or atexit handlers that belong to the parent.
 *
 * The child gets a copy-on-write image of the parent's memory, so \p task
 * sees the keyspace exactly as it was at fork time while the parent keeps
 * serving clients.
 *
 * \return The child's process id (in the parent).
 */
template <typename Task>
std::expected<pid_type, std::error_code> fork_and_run(Task &&task);

/**
 * \brief Reap \p child if it has exited, without blocking unless \p wait.
 * \return std::nullopt while the child is still running; otherwise whether
 *         it exited with status 0.
 */
[[nodiscard]] std::optional<bool> reap(pid_type child, bool wait);

/// The calling process' id.
[[nodiscard]] pid_type current_pid() noexcept;

/// fsync() the file at \p path so its contents are durable.
[[nodiscard]] std::error_code sync_file(const std::filesystem::path &path);

/// fsync() the directory \p dir so a rename inside it is durable.
[[nodiscard]] std::error_code sync_directory(const std::filesystem::path &dir);

namespace detail {
[[nodiscard]] std::expected<pid_type, std::error_code> fork_process();
[[noreturn]] void exit_child(bool success) noexcept;
} // namespace detail

template <typename Task>
std::expected<pid_type, std::error_code> fork_and_run(Task &&task) {
	auto pid = detail::fork_process();
	if (pid && *pid == 0) {
		bool success = false;
		try {
			success = task();
		} catch (...) {
		}
		detail::exit_child(success);
	}
	return pid;
}

} // namespace redis::persistence::posix
