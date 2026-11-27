// The sandbox (ADR-0014; TC-334, TC-335). Each scenario sandboxes a forked
// child and looks at how it ends: the restrictions cannot be undone, and must
// not reach the test runner.
#include "core/os/sandbox.hpp"

#include "core/os/detail/sandbox_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <string_view>

#if REDIS_HAS_SANDBOX
#include <fcntl.h>
#include <signal.h> // NOLINT(modernize-deprecated-headers): signal()
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <system_error>

namespace {
namespace fs = std::filesystem;
using redis::os::Confinement;

/// How a child ended.
struct Ending {
	bool is_signalled = false;
	int signal        = 0;
	int status        = -1; ///< the exit status, when not signalled
};

/// What a child reports through its exit status.
enum ChildExit : int {
	PASSED       = 0,
	FAILED       = 1,
	NOT_APPLIED  = 77, ///< the kernel has no such sandbox
	SETUP_FAILED = 78,
};

/// Run \p body in a forked child, which exits with what it returns.
template <typename Body>
Ending in_child(Body body) {
	const pid_t pid = ::fork();
	REQUIRE(pid >= 0);
	if (pid == 0) {
		// The default action: the runner's handlers are not the child's.
		::signal(SIGSYS, SIG_DFL);
		::_exit(body());
	}
	int status = 0;
	REQUIRE(::waitpid(pid, &status, 0) == pid);
	if (WIFSIGNALED(status))
		return {.is_signalled = true, .signal = WTERMSIG(status), .status = -1};
	return {.is_signalled = false, .signal = 0, .status = WEXITSTATUS(status)};
}

/// Confines system calls, then makes a call that is never allowed.
int call_ptrace() {
	const auto confined = redis::os::confine_syscalls();
	if (!confined || *confined != Confinement::Applied) return SETUP_FAILED;
	::syscall(SYS_ptrace, 0, 0, 0, 0); // PTRACE_TRACEME: refused, never run
	return FAILED;                     // only reached if the call ran
}

/// Confines system calls, then makes only allowed ones.
int stay_within_allowed() {
	const auto confined = redis::os::confine_syscalls();
	if (!confined || *confined != Confinement::Applied) return SETUP_FAILED;
	return ::getpid() > 0 ? PASSED : FAILED;
}

/// A new empty directory, removed with what is in it.
class TempDir {
public:
	TempDir() {
		std::string pattern = (fs::temp_directory_path() / "sandbox-XXXXXX");
		REQUIRE(::mkdtemp(pattern.data()) != nullptr);
		path_ = pattern;
	}
	TempDir(const TempDir &)            = delete;
	TempDir &operator=(const TempDir &) = delete;
	~TempDir() {
		std::error_code ignored;
		fs::remove_all(path_, ignored);
	}
	[[nodiscard]] const fs::path &path() const { return path_; }

private:
	fs::path path_;
};

bool can_create(const fs::path &file) {
	const int fd = ::open(file.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
	if (fd < 0) return false;
	::close(fd);
	return true;
}

/// Confines files to \p allowed, then creates a file there and one in
/// \p other, which must be refused with EACCES.
int create_inside_and_outside(const fs::path &allowed, const fs::path &other) {
	const std::array read_write{allowed};
	const auto confined =
		redis::os::confine_paths({.read_write = read_write, .read_only = {}});
	if (!confined) return SETUP_FAILED;
	if (*confined == Confinement::NotSupported) return NOT_APPLIED;
	if (!can_create(allowed / "inside")) return FAILED;
	if (can_create(other / "outside") || errno != EACCES) return FAILED;
	return PASSED;
}

} // namespace

SCENARIO("A call outside the allow-list is refused", "[sandbox][fork]") {
	WHEN("a confined child calls ptrace") {
		const Ending ending = in_child(call_ptrace);

		THEN("it ends with SIGSYS, the call not run") {
			CHECK(ending.is_signalled);
			CHECK(ending.signal == SIGSYS);
		}
	}
	WHEN("a confined child makes only allowed calls") {
		const Ending ending = in_child(stay_within_allowed);

		THEN("it exits normally") {
			CHECK_FALSE(ending.is_signalled);
			CHECK(ending.status == PASSED);
		}
	}
	THEN("refused calls are named in a report") {
		CHECK(redis::os::syscall_name(SYS_ptrace) == "ptrace");
		CHECK(redis::os::syscall_name(SYS_socket) == "socket");
		CHECK(redis::os::syscall_name(SYS_read) == "read");
		CHECK(redis::os::syscall_name(-1).empty());
	}
}

SCENARIO("Landlock confines files to the allowed directories",
		 "[sandbox][fork]") {
	GIVEN("an allowed directory and another") {
		const TempDir allowed;
		const TempDir other;

		WHEN("a child confined to the first creates a file in each") {
			const Ending ending = in_child([&] {
				return create_inside_and_outside(allowed.path(), other.path());
			});

			if (ending.status == NOT_APPLIED) SKIP("this kernel has no Landlock");
			THEN("only the one inside is created") {
				CHECK_FALSE(ending.is_signalled);
				CHECK(ending.status == PASSED);
			}
		}
	}
}

#else

SCENARIO("Without a sandbox the calls restrict nothing", "[sandbox]") {
	using redis::os::Confinement;
	const std::array<std::filesystem::path, 0> none{};
	CHECK(redis::os::confine_paths({.read_write = none, .read_only = none}) ==
		  Confinement::NotSupported);
	CHECK(redis::os::syscall_name(0).empty());
}

#endif
