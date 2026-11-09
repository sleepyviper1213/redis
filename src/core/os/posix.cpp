#include "child_process.hpp"
#include "durable_file.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace redis::os {
static_assert(std::is_same_v<pid_t, pid_type>,
			  "pid_type must match the platform's pid_t");

namespace {
std::error_code last_error() { return {errno, std::generic_category()}; }

// The longest failure message the child sends. Far below the pipe buffer, so
// its write never blocks, even before the parent has read anything.
constexpr size_t MAX_REPORT_SIZE = 1024;

// What the child wrote before it ended. Called after waitpid() reported it
// gone and the parent closed its copy of the write end, so nothing can still be
// writing: this returns what is in the pipe, or nothing, without blocking.
std::string read_report(int fd) {
	std::string text(MAX_REPORT_SIZE, '\0');
	ssize_t got = ::read(fd, text.data(), text.size());
	while (got == -1 && errno == EINTR)
		got = ::read(fd, text.data(), text.size());
	text.resize(got > 0 ? static_cast<size_t>(got) : 0);
	return text;
}

// Close every descriptor but stdio and \p keep. The child needs none of what it
// inherited, and holding the server's sockets is harmful: the listening socket
// would keep the port bound if the parent died, and a client socket the parent
// closed would not really close until the save ended. A plain close(): these
// are the child's copies, so the parent's descriptors, and what is registered
// with an epoll set the two share, stay as they are (asio's own close() would
// deregister the socket from that set, for the parent too).
void close_inherited_descriptors(int keep) {
	constexpr int FIRST_NON_STDIO_FD = 3;
	constexpr long MAX_FALLBACK_FDS  = 65536;

	std::vector<int> fds;
	if (DIR *dir = ::opendir("/dev/fd")) {
		const int dir_fd = ::dirfd(dir);
		while (const dirent *entry = ::readdir(dir)) {
			char *end     = nullptr;
			const long fd = std::strtol(entry->d_name, &end, 10);
			// "." and ".." parse as nothing; the directory's own descriptor is
			// in its own listing.
			if (end != entry->d_name && *end == '\0' &&
				fd >= FIRST_NON_STDIO_FD && fd != keep && fd != dir_fd)
				fds.push_back(static_cast<int>(fd));
		}
		::closedir(dir);
	} else {
		// No /dev/fd to list: try everything that could be open.
		const long open_max = ::sysconf(_SC_OPEN_MAX);
		const long limit =
			open_max > 0 ? std::min(open_max, MAX_FALLBACK_FDS) : MAX_FALLBACK_FDS;
		for (int fd = FIRST_NON_STDIO_FD; fd < limit; ++fd)
			if (fd != keep) fds.push_back(fd);
	}
	for (const int fd : fds) ::close(fd);
}

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
void close_fd(int fd) noexcept {
	if (fd >= 0) ::close(fd);
}

std::expected<Child, std::error_code> fork_process() {
	std::array<int, 2> pipe_fds{-1, -1};
	if (::pipe(pipe_fds.data()) == -1) return std::unexpected(last_error());
	const auto [read_fd, write_fd] = pipe_fds;
	// The parent only polls it: reading must never block it.
	(void)::fcntl(read_fd, F_SETFL, O_NONBLOCK);

	const pid_t pid = ::fork();
	if (pid == -1) {
		const std::error_code ec = last_error();
		::close(read_fd);
		::close(write_fd);
		return std::unexpected(ec);
	}
	if (pid == 0) {
		::close(read_fd); // the child only writes
		close_inherited_descriptors(write_fd);
		return Child(0, write_fd);
	}
	// The parent only reads. With its copy of the write end closed, end of
	// file on the read end means the child is gone.
	::close(write_fd);
	return Child(pid, read_fd);
}

void exit_child(int report_fd, bool success, std::string_view error) noexcept {
	if (!success && !error.empty()) {
		// Best effort: if this fails there is nobody left to tell.
		[[maybe_unused]] const ssize_t written = ::write(
			report_fd, error.data(), std::min(error.size(), MAX_REPORT_SIZE));
	}
	// _exit, not exit: the child shares the parent's stdio buffers and
	// static objects (loggers, sockets), which must not be flushed or
	// destroyed twice.
	::_exit(success ? 0 : 1);
}
} // namespace detail

pid_type current_pid() noexcept { return ::getpid(); }

std::optional<ChildResult> reap(const Child &child, bool wait) {
	int status      = 0;
	const int flags = wait ? 0 : WNOHANG;
	pid_t result    = ::waitpid(child.pid(), &status, flags);
	// A signal can interrupt the wait; just wait again.
	while (result == -1 && errno == EINTR)
		result = ::waitpid(child.pid(), &status, flags);

	if (result == 0) return std::nullopt; // still running
	if (result == -1)
		return ChildResult{.success = false,
						   .error   = "the child is gone and its exit status "
									  "was lost"};
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return ChildResult{.success = true, .error = {}};

	// The child's own explanation if it wrote one; otherwise just how it ended
	// (killed by a signal never gets to write).
	ChildResult failed{.success = false, .error = read_report(child.report_fd())};
	if (failed.error.empty())
		failed.error =
			WIFEXITED(status)
				? fmt::format("exited with status {}", WEXITSTATUS(status))
				: fmt::format("killed by signal {}", WTERMSIG(status));
	return failed;
}

void kill_child(pid_type child) noexcept {
	// ESRCH, the child being gone already, is fine: there is nothing to stop.
	(void)::kill(child, SIGKILL);
}

std::error_code sync_file(const std::filesystem::path &path) {
	return fsync_path(path, O_RDONLY);
}

std::error_code sync_directory(const std::filesystem::path &dir) {
	return fsync_path(dir.empty() ? "." : dir, O_RDONLY | O_DIRECTORY);
}

// ---- DurableFile -------------------------------------------------------------------

namespace {
// What the server creates is the owner's: readable by nobody else.
constexpr mode_t PRIVATE_FILE_MODE = S_IRUSR | S_IWUSR;

// O_CLOEXEC: no program the server starts inherits the descriptor.
// O_NOFOLLOW: a link at the last component fails (ELOOP) instead of being
// followed.
std::expected<int, std::error_code> open_fd(const std::filesystem::path &path,
											int flags) {
	int fd = -1;
	do {
		fd = ::open(path.c_str(), flags | O_CLOEXEC, PRIVATE_FILE_MODE);
	} while (fd == -1 && errno == EINTR);
	if (fd == -1) return std::unexpected(last_error());
	return fd;
}
} // namespace

std::expected<DurableFile, std::error_code>
DurableFile::open_append(const std::filesystem::path &path) {
	const auto fd = open_fd(path, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW);
	if (!fd) return std::unexpected(fd.error());
	return DurableFile(*fd);
}

std::expected<DurableFile, std::error_code>
DurableFile::create_new(const std::filesystem::path &path) {
	const auto fd = open_fd(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW);
	if (!fd) return std::unexpected(fd.error());
	return DurableFile(*fd);
}

std::expected<DurableFile, std::error_code>
DurableFile::open_existing(const std::filesystem::path &path, Access access) {
	// Reading follows a link (a log kept on another disk is legitimate);
	// anything that may cut the file does not.
	const int flags = access == Access::read ? O_RDONLY : O_RDWR | O_NOFOLLOW;
	const auto fd   = open_fd(path, flags);
	if (!fd) return std::unexpected(fd.error());
	return DurableFile(*fd);
}

void DurableFile::close() noexcept {
	if (fd_ >= 0) ::close(fd_);
	fd_ = -1;
}

std::error_code DurableFile::write_all(std::string_view data) {
	while (!data.empty()) {
		const ssize_t wrote = ::write(fd_, data.data(), data.size());
		if (wrote == -1) {
			if (errno == EINTR) continue;
			return last_error();
		}
		data.remove_prefix(static_cast<size_t>(wrote));
	}
	return {};
}

std::expected<std::string, std::error_code> DurableFile::read_all() const {
	std::string content;
	std::array<char, 64 * 1024> chunk{};
	for (;;) {
		const ssize_t got = ::read(fd_, chunk.data(), chunk.size());
		if (got == -1) {
			if (errno == EINTR) continue;
			return std::unexpected(last_error());
		}
		if (got == 0) return content;
		content.append(chunk.data(), static_cast<size_t>(got));
	}
}

std::expected<std::uintmax_t, std::error_code> DurableFile::size() const {
	struct stat info{};
	if (::fstat(fd_, &info) == -1) return std::unexpected(last_error());
	return static_cast<std::uintmax_t>(info.st_size);
}

std::error_code DurableFile::truncate(std::uintmax_t size) {
	if (::ftruncate(fd_, static_cast<off_t>(size)) == -1) return last_error();
	return {};
}

std::error_code DurableFile::sync() {
	if (durable_sync(fd_) == -1) return last_error();
	return {};
}

std::error_code DurableFile::try_lock() {
	int locked = -1;
	do {
		locked = ::flock(fd_, LOCK_EX | LOCK_NB);
	} while (locked == -1 && errno == EINTR);
	// EWOULDBLOCK: someone else holds it.
	return locked == -1 ? last_error() : std::error_code{};
}

} // namespace redis::os
