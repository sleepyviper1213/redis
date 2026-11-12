#pragma once

#include <fmt/format.h>

#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// The only POSIX-specific code in the server: process management for
// background saves, and fsync for durable snapshot writes.
namespace redis::os {

/// Process id of a child created by fork_and_run().
using pid_type = int;

namespace detail {
/// Close \p fd, the report descriptor of a Child. Does nothing for -1.
void close_fd(int fd) noexcept;
} // namespace detail

/**
 * \brief A child created by fork_and_run(), and the read end of the pipe on
 * which it reports why it failed.
 *
 * The child cannot log (the logger belongs to the parent's threads), but it can
 * write to a pipe. Move-only: it owns the descriptor and closes it when it is
 * destroyed, so a child that is never reaped does not leak it.
 */
class Child {
public:
	Child() = default;
	Child(pid_type pid, int report_fd) noexcept
		: pid_(pid),
		  report_fd_(report_fd) {}

	Child(const Child &)            = delete;
	Child &operator=(const Child &) = delete;

	Child(Child &&other) noexcept
		: pid_(other.pid_),
		  report_fd_(std::exchange(other.report_fd_, -1)) {}

	Child &operator=(Child &&other) noexcept {
		if (this != &other) {
			detail::close_fd(report_fd_);
			pid_       = other.pid_;
			report_fd_ = std::exchange(other.report_fd_, -1);
		}
		return *this;
	}

	~Child() { detail::close_fd(report_fd_); }

	[[nodiscard]] pid_type pid() const noexcept { return pid_; }
	/// In the parent the pipe's read end; in the child its write end.
	[[nodiscard]] int report_fd() const noexcept { return report_fd_; }

private:
	pid_type pid_ = -1;
	int report_fd_ = -1;
};

/// How a child ended.
struct ChildResult {
	/// It exited with status 0.
	bool success = false;
	/// Why it did not, when it did not: the child's own message if it got to
	/// write one, otherwise how the process ended ("killed by signal 9").
	std::string error;
};

/**
 * \brief fork() the process. The child runs \p task, which returns
 * std::expected<void, E> for any E that fmt can format, then exits immediately
 * with status 0 on success and 1 otherwise, without running destructors or
 * atexit handlers that belong to the parent. On failure the child first writes
 * the error's text (the task's, or the exception's) to the pipe the parent
 * reads in reap(): only text can cross the process boundary, so this is where
 * the error is rendered.
 *
 * The child gets a copy-on-write image of the parent's memory, so \p task
 * sees the keyspace exactly as it was at fork time while the parent keeps
 * serving clients.
 *
 * \return The child (in the parent).
 */
template <typename Task>
std::expected<Child, std::error_code> fork_and_run(Task &&task);

/**
 * \brief Reap \p child if it has exited, without blocking unless \p wait.
 * \return std::nullopt while the child is still running; otherwise how it
 *         ended. Leaves \p child's pipe open; destroy the Child to close it.
 */
[[nodiscard]] std::optional<ChildResult> reap(const Child &child, bool wait);

/**
 * \brief Kill \p child with SIGKILL. Not a catchable signal: the child is a
 * copy of the server and inherits its signal handlers, which would swallow
 * SIGTERM. Does nothing if the child is already gone. Follow with reap().
 */
void kill_child(pid_type child) noexcept;

/**
 * \brief Keep \p fd open in every child fork_and_run() creates, which
 * otherwise closes everything but stdio and its report pipe.
 *
 * For descriptors the child must still write to: the crash handler's log
 * file, so a crash in a BGSAVE child is reported where the parent's would be.
 * Closed, its number could be reused by the child's next open(), the snapshot
 * file's, and the report would be written into the snapshot instead.
 *
 * Call before the first fork, from one thread (at startup). At most a few
 * descriptors; further ones are ignored. Does nothing on Windows.
 */
void keep_open_in_children(int fd) noexcept;

/// The calling process' id.
[[nodiscard]] pid_type current_pid() noexcept;

/// fsync() the file at \p path so its contents are durable.
[[nodiscard]] std::error_code sync_file(const std::filesystem::path &path);

/// fsync() the directory \p dir so a rename inside it is durable.
[[nodiscard]] std::error_code sync_directory(const std::filesystem::path &dir);

namespace detail {
/// fork() with a pipe for the child's report. In the child the result has pid 0
/// and holds the pipe's write end; in the parent, the child's pid and the read
/// end. The child closes every other descriptor it inherited but stdio, so it
/// does not hold the server's listening and client sockets while it runs.
[[nodiscard]] std::expected<Child, std::error_code> fork_process();
/// Send \p error (cut to fit the pipe) on \p report_fd, then end the process
/// without running anything of the parent's.
[[noreturn]] void exit_child(int report_fd, bool success,
							 std::string_view error) noexcept;
} // namespace detail

template <typename Task>
std::expected<Child, std::error_code> fork_and_run(Task &&task) {
	auto child = detail::fork_process();
	if (child && child->pid() == 0) {
		bool success = false;
		std::string error;
		try {
			const auto result = task();
			success           = result.has_value();
			if (!result) error = fmt::format("{}", result.error());
		} catch (const std::exception &e) {
			// Not rethrown: unwinding would run the parent's destructors in
			// this copy of it. The failure is reported instead.
			error = e.what();
		} catch (...) {
			error = "unknown exception";
		}
		detail::exit_child(child->report_fd(), success, error);
	}
	return child;
}

} // namespace redis::os
