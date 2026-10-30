// Development builds on Windows (MinGW) only. Windows cannot fork, so BGSAVE
// fails with an error there; SAVE and shutdown saves work.
#include "posix.hpp"

#include <windows.h>

#include <cstdlib>
#include <process.h>

namespace redis::persistence::posix {

namespace detail {
std::expected<pid_type, std::error_code> fork_process() {
	return std::unexpected(
		std::make_error_code(std::errc::function_not_supported));
}

void exit_child(bool success) noexcept { std::_Exit(success ? 0 : 1); }
} // namespace detail

pid_type current_pid() noexcept { return ::_getpid(); }

// fork_process() never succeeds, so there is never a child to reap.
std::optional<bool> reap(pid_type, bool) { return false; }

std::error_code sync_file(const std::filesystem::path &path) {
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE,
								FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
								OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return {static_cast<int>(::GetLastError()), std::system_category()};
	std::error_code ec;
	if (!::FlushFileBuffers(file))
		ec = {static_cast<int>(::GetLastError()), std::system_category()};
	::CloseHandle(file);
	return ec;
}

// Windows has no way to flush a directory entry; NTFS journals renames.
std::error_code sync_directory(const std::filesystem::path &) { return {}; }

} // namespace redis::persistence::posix
