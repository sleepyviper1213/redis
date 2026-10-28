#include "posix.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <type_traits>

namespace redis::persistence::posix {
static_assert(std::is_same_v<pid_t, pid_type>,
			  "pid_type must match the platform's pid_t");

namespace {
std::error_code last_error() { return {errno, std::generic_category()}; }

// Flush fd's data to stable storage.
int durable_sync(int fd) {
#ifdef F_FULLFSYNC
	// On macOS fsync() only hands the data to the drive, which may keep it in
	// a volatile cache; F_FULLFSYNC asks the drive to flush that cache too.
	// Some filesystems (e.g. network mounts) reject it, so fall back.
	if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;
#endif
	return ::fsync(fd);
}

std::error_code fsync_path(const std::filesystem::path &path, int flags) {
	const int fd = ::open(path.c_str(), flags);
	if (fd == -1) return last_error();
	std::error_code ec;
	if (durable_sync(fd) == -1) ec = last_error();
	::close(fd);
	return ec;
}
} // namespace

namespace detail {
std::expected<pid_type, std::error_code> fork_process() {
	const pid_t pid = ::fork();
	if (pid == -1) return std::unexpected(last_error());
	return pid;
}

void exit_child(bool success) noexcept {
	// _exit, not exit: the child shares the parent's stdio buffers and
	// static objects (loggers, sockets), which must not be flushed or
	// destroyed twice.
	::_exit(success ? 0 : 1);
}
} // namespace detail

pid_type current_pid() noexcept { return ::getpid(); }

std::optional<bool> reap(pid_type child, bool wait) {
	int status = 0;
	pid_t result = 0;
	do {
		result = ::waitpid(child, &status, wait ? 0 : WNOHANG);
	} while (result == -1 && errno == EINTR);

	if (result == 0) return std::nullopt; // still running
	if (result == -1) return false;       // not our child any more
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::error_code sync_file(const std::filesystem::path &path) {
	return fsync_path(path, O_RDONLY);
}

std::error_code sync_directory(const std::filesystem::path &dir) {
	return fsync_path(dir.empty() ? "." : dir, O_RDONLY | O_DIRECTORY);
}

} // namespace redis::persistence::posix
