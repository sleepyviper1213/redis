#pragma once
// Least privilege (ADR-0014): what the process may open, and which system
// calls it may make, from some point on. Linux only (Landlock and seccomp);
// elsewhere, and in sanitizer builds, every call reports NotSupported and
// restricts nothing.

#include "core/error/failure_format.hpp" // IWYU pragma: keep (formats every Failure)

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace redis::os {

enum class SandboxKind : std::uint8_t {
	CannotOpenPath, ///< [path, cause]: a directory to allow could not be opened
	Landlock,       ///< [cause]: the ruleset could not be built or applied
	Seccomp,        ///< [cause]: the filter could not be installed
};

using SandboxError = Failure<SandboxKind>;

/// "sandbox: cannot open <path>: <reason>", "sandbox: Landlock failed: ...".
[[nodiscard]] std::string display(const SandboxError &error);

/// Whether a restriction is now in force.
enum class Confinement : std::uint8_t {
	Applied,
	NotSupported, ///< this platform, kernel or (sanitizer) build has none
};

[[nodiscard]] std::string_view format_as(Confinement confinement) noexcept;

/// The paths the process keeps, and how.
struct SandboxPaths {
	/// Read, create, replace and delete files beneath each.
	std::span<const std::filesystem::path> read_write;
	/// Read beneath each; a path naming a file allows only that file.
	std::span<const std::filesystem::path> read_only;
};

/**
 * \brief From now on, the calling thread and every thread it starts can open
 * only what \p paths allow; anything else fails with EACCES (Landlock).
 *
 * Threads that already exist are not restricted: call it before starting
 * any. A path that does not exist is skipped. A kernel without Landlock gives
 * NotSupported, not an error: the seccomp filter still applies.
 */
[[nodiscard]] std::expected<Confinement, SandboxError>
confine_paths(const SandboxPaths &paths);

/**
 * \brief From now on, every thread of the process may make only the system
 * calls the server needs (seccomp, all threads at once). Any other call is not
 * run: it raises SIGSYS, which the crash handler reports by name before the
 * process aborts.
 *
 * Call it once everything is set up: the data loaded, the port bound. Sockets
 * cannot be created, nor programs started, after it.
 */
[[nodiscard]] std::expected<Confinement, SandboxError> confine_syscalls();

/// The name of system call \p number on this platform, or "" if it has none
/// here. Async-signal-safe: the crash handler names a refused call with it.
[[nodiscard]] std::string_view syscall_name(long number) noexcept;

} // namespace redis::os
