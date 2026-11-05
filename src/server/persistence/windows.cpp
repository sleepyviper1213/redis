// Development builds on Windows (MinGW) only. Windows cannot fork, so BGSAVE
// fails with an error there; SAVE and shutdown saves work.
#include "child_process.hpp"

#include <windows.h>

#include <cstdlib>
#include <process.h>

namespace redis::persistence::posix {

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

} // namespace redis::persistence::posix
