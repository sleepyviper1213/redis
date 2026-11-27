// The sandbox's wording, and the sandbox where there is none (ADR-0014). The
// Linux implementation is sandbox_linux.cpp.
#include "sandbox.hpp"

#include "detail/sandbox_support.hpp"

#include <fmt/format.h>
#include <fmt/std.h>

namespace redis::os {

std::string display(const SandboxError &error) {
	switch (error.kind) {
	case SandboxKind::CannotOpenPath:
		return fmt::format("sandbox: cannot open {}: {}",
						   error.path,
						   error.cause.message());
	case SandboxKind::Landlock:
		return fmt::format("sandbox: Landlock failed: {}",
						   error.cause.message());
	case SandboxKind::Seccomp:
		return fmt::format("sandbox: seccomp failed: {}",
						   error.cause.message());
	}
	return "sandbox failed";
}

std::string_view format_as(Confinement confinement) noexcept {
	switch (confinement) {
	case Confinement::Applied: return "applied";
	case Confinement::NotSupported: return "not supported";
	}
	return "unknown";
}

#if !REDIS_HAS_SANDBOX
std::expected<Confinement, SandboxError>
confine_paths(const SandboxPaths & /*paths*/) {
	return Confinement::NotSupported;
}

std::expected<Confinement, SandboxError> confine_syscalls() {
	return Confinement::NotSupported;
}

std::string_view syscall_name(long /*number*/) noexcept { return {}; }
#endif

} // namespace redis::os
