// Development builds on Windows (MinGW) only. Windows cannot fork, so BGSAVE
// fails with an error there; SAVE and shutdown saves work.
#include "child_process.hpp"
#include "durable_file.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>

namespace redis::os {

namespace detail {
// There is never a report pipe.
void close_fd(int /*fd*/) noexcept {}

std::expected<Child, std::error_code> fork_process() {
	return std::unexpected(
		std::make_error_code(std::errc::function_not_supported));
}

void exit_child(int /*report_fd*/, bool success,
				std::string_view /*error*/) noexcept {
	std::_Exit(success ? 0 : 1);
}
} // namespace detail

pid_type current_pid() noexcept { return ::_getpid(); }

// fork_process() never succeeds, so there is never a child to reap.
std::optional<ChildResult> reap(const Child & /*child*/, bool /*wait*/) {
	return ChildResult{.success = false,
					   .error   = "background saving is not supported here"};
}

// There is never a child to kill.
void kill_child(pid_type /*child*/) noexcept {}

// There are never children.
void keep_open_in_children(int /*fd*/) noexcept {}

std::error_code sync_file(const std::filesystem::path &path) {
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE,
								FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
								OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return {static_cast<int>(::GetLastError()), std::system_category()};
	std::error_code ec;
	if (::FlushFileBuffers(file) == 0)
		ec = {static_cast<int>(::GetLastError()), std::system_category()};
	::CloseHandle(file);
	return ec;
}

// Windows has no way to flush a directory entry; NTFS journals renames.
std::error_code sync_directory(const std::filesystem::path & /*dir*/) {
	return {};
}

// ---- DurableFile -------------------------------------------------------------------
// Through the C runtime's descriptors, which keeps DurableFile a plain int. There is no
// symbolic-link check here (Windows has no O_NOFOLLOW), nor a file mode.

namespace {
std::error_code last_error() { return {errno, std::generic_category()}; }

constexpr int COMMON_FLAGS = _O_BINARY | _O_NOINHERIT;

std::expected<int, std::error_code> open_fd(const std::filesystem::path &path,
											int flags) {
	const int fd = ::_wopen(path.c_str(), flags | COMMON_FLAGS,
							_S_IREAD | _S_IWRITE);
	if (fd == -1) return std::unexpected(last_error());
	return fd;
}

// Largest piece handed to one _write/_read, which take an unsigned count.
constexpr size_t MAX_IO = 1U << 30;
} // namespace

std::expected<DurableFile, std::error_code>
DurableFile::open_append(const std::filesystem::path &path) {
	const auto fd = open_fd(path, _O_WRONLY | _O_APPEND | _O_CREAT);
	if (!fd) return std::unexpected(fd.error());
	return DurableFile(*fd);
}

std::expected<DurableFile, std::error_code>
DurableFile::create_new(const std::filesystem::path &path) {
	const auto fd = open_fd(path, _O_WRONLY | _O_CREAT | _O_EXCL);
	if (!fd) return std::unexpected(fd.error());
	return DurableFile(*fd);
}

std::expected<DurableFile, std::error_code>
DurableFile::open_existing(const std::filesystem::path &path, Access access) {
	const auto fd =
		open_fd(path, access == Access::read ? _O_RDONLY : _O_RDWR);
	if (!fd) return std::unexpected(fd.error());
	return DurableFile(*fd);
}

void DurableFile::close() noexcept {
	if (fd_ >= 0) ::_close(fd_);
	fd_ = -1;
}

std::error_code DurableFile::write_all(std::string_view data) {
	while (!data.empty()) {
		const auto piece = std::min(data.size(), MAX_IO);
		const int wrote  = ::_write(fd_, data.data(), static_cast<unsigned>(piece));
		if (wrote <= 0) return last_error();
		data.remove_prefix(static_cast<size_t>(wrote));
	}
	return {};
}

std::expected<std::string, std::error_code> DurableFile::read_all() const {
	std::string content;
	std::array<char, 64 * 1024> chunk{};
	for (;;) {
		const int got =
			::_read(fd_, chunk.data(), static_cast<unsigned>(chunk.size()));
		if (got < 0) return std::unexpected(last_error());
		if (got == 0) return content;
		content.append(chunk.data(), static_cast<size_t>(got));
	}
}

std::expected<std::uintmax_t, std::error_code> DurableFile::size() const {
	const __int64 length = ::_filelengthi64(fd_);
	if (length < 0) return std::unexpected(last_error());
	return static_cast<std::uintmax_t>(length);
}

std::error_code DurableFile::truncate(std::uintmax_t size) {
	if (const errno_t err = ::_chsize_s(fd_, static_cast<__int64>(size)); err != 0)
		return {err, std::generic_category()};
	return {};
}

std::error_code DurableFile::sync() {
	if (::_commit(fd_) == -1) return last_error();
	return {};
}

std::error_code DurableFile::try_lock() {
	const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd_));
	OVERLAPPED where{};
	if (::LockFileEx(handle,
					 LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
					 0,
					 1,
					 0,
					 &where) != 0)
		return {};
	const DWORD err = ::GetLastError();
	if (err == ERROR_LOCK_VIOLATION)
		return std::make_error_code(std::errc::resource_unavailable_try_again);
	return {static_cast<int>(err), std::system_category()};
}

} // namespace redis::os
