// The sandbox on Linux (ADR-0014): Landlock for paths, a seccomp allow-list
// for system calls. Compiled everywhere but POSIX-only in effect: off Linux
// x86-64, and in sanitizer builds, sandbox.cpp has the no-op versions.
#include "detail/sandbox_support.hpp"

#if REDIS_HAS_SANDBOX

#include "core/error/failed.hpp"
#include "core/error/try.hpp"
#include "sandbox.hpp"

#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace redis::os {
namespace {

struct Syscall {
	long number;
	std::string_view name;
};

#define REDIS_SYSCALL(name) Syscall{__NR_##name, #name}

/// Every system call the server makes once serving, its children (BGSAVE,
/// BGREWRITEAOF) and the crash handler included. Grouped by what needs it; a
/// missing one shows as a SIGSYS report naming it (TC-336).
constexpr std::array ALLOWED{
	// Memory: malloc, jemalloc, thread stacks and their guard pages.
	REDIS_SYSCALL(brk),
	REDIS_SYSCALL(mmap),
	REDIS_SYSCALL(munmap),
	REDIS_SYSCALL(mremap),
	REDIS_SYSCALL(mprotect),
	REDIS_SYSCALL(madvise),
	// Threads, locks, signals: std::thread, std::mutex, Asio's signal_set, the
	// crash handler (an alternate stack per thread, alarm() as its timeout).
	REDIS_SYSCALL(futex),
	REDIS_SYSCALL(clone),
#ifdef __NR_clone3
	REDIS_SYSCALL(clone3),
#endif
	REDIS_SYSCALL(fork),
	REDIS_SYSCALL(set_robust_list),
#ifdef __NR_rseq
	REDIS_SYSCALL(rseq),
#endif
	REDIS_SYSCALL(set_tid_address),
	REDIS_SYSCALL(sched_yield),
	REDIS_SYSCALL(sched_getaffinity),
	REDIS_SYSCALL(prctl),
	REDIS_SYSCALL(arch_prctl),
	REDIS_SYSCALL(rt_sigaction),
	REDIS_SYSCALL(rt_sigprocmask),
	REDIS_SYSCALL(rt_sigreturn),
	REDIS_SYSCALL(sigaltstack),
	REDIS_SYSCALL(alarm),
	REDIS_SYSCALL(setitimer),
	REDIS_SYSCALL(restart_syscall),
	// Processes: reaping and cancelling a background save, abort(), exit.
	REDIS_SYSCALL(wait4),
	REDIS_SYSCALL(waitid),
	REDIS_SYSCALL(kill),
	REDIS_SYSCALL(tgkill),
	REDIS_SYSCALL(tkill),
	REDIS_SYSCALL(getpid),
	REDIS_SYSCALL(gettid),
	REDIS_SYSCALL(getppid),
	REDIS_SYSCALL(getuid),
	REDIS_SYSCALL(geteuid),
	REDIS_SYSCALL(getgid),
	REDIS_SYSCALL(getegid),
	REDIS_SYSCALL(exit),
	REDIS_SYSCALL(exit_group),
	// Time.
	REDIS_SYSCALL(clock_gettime),
	REDIS_SYSCALL(clock_getres),
	REDIS_SYSCALL(gettimeofday),
	REDIS_SYSCALL(time),
	REDIS_SYSCALL(nanosleep),
	REDIS_SYSCALL(clock_nanosleep),
	REDIS_SYSCALL(timerfd_create),
	REDIS_SYSCALL(timerfd_settime),
	REDIS_SYSCALL(timerfd_gettime),
	// The event loop.
	REDIS_SYSCALL(epoll_create1),
	REDIS_SYSCALL(epoll_ctl),
	REDIS_SYSCALL(epoll_wait),
	REDIS_SYSCALL(epoll_pwait),
#ifdef __NR_epoll_pwait2
	REDIS_SYSCALL(epoll_pwait2),
#endif
	REDIS_SYSCALL(eventfd2),
	REDIS_SYSCALL(poll),
	REDIS_SYSCALL(ppoll),
	REDIS_SYSCALL(pselect6),
	REDIS_SYSCALL(pipe),
	REDIS_SYSCALL(pipe2),
	// Sockets the server already has: the listening one and those it accepts.
	// No socket(), bind(), connect() or listen().
	REDIS_SYSCALL(accept),
	REDIS_SYSCALL(accept4),
	REDIS_SYSCALL(recvfrom),
	REDIS_SYSCALL(recvmsg),
	REDIS_SYSCALL(recvmmsg),
	REDIS_SYSCALL(sendto),
	REDIS_SYSCALL(sendmsg),
	REDIS_SYSCALL(sendmmsg),
	REDIS_SYSCALL(shutdown),
	REDIS_SYSCALL(getsockname),
	REDIS_SYSCALL(getpeername),
	REDIS_SYSCALL(setsockopt),
	REDIS_SYSCALL(getsockopt),
	// Descriptors and files; which paths can be opened is Landlock's.
	REDIS_SYSCALL(read),
	REDIS_SYSCALL(write),
	REDIS_SYSCALL(readv),
	REDIS_SYSCALL(writev),
	REDIS_SYSCALL(pread64),
	REDIS_SYSCALL(pwrite64),
	REDIS_SYSCALL(lseek),
	REDIS_SYSCALL(close),
#ifdef __NR_close_range
	REDIS_SYSCALL(close_range),
#endif
	REDIS_SYSCALL(dup),
	REDIS_SYSCALL(dup2),
	REDIS_SYSCALL(dup3),
	REDIS_SYSCALL(fcntl),
	REDIS_SYSCALL(ioctl),
	REDIS_SYSCALL(open),
	REDIS_SYSCALL(openat),
	REDIS_SYSCALL(stat),
	REDIS_SYSCALL(lstat),
	REDIS_SYSCALL(fstat),
	REDIS_SYSCALL(newfstatat),
	REDIS_SYSCALL(statx),
	REDIS_SYSCALL(fstatfs),
	REDIS_SYSCALL(access),
	REDIS_SYSCALL(faccessat),
#ifdef __NR_faccessat2
	REDIS_SYSCALL(faccessat2),
#endif
	REDIS_SYSCALL(getdents64),
	REDIS_SYSCALL(getcwd),
	REDIS_SYSCALL(readlink),
	REDIS_SYSCALL(readlinkat),
	REDIS_SYSCALL(mkdir),
	REDIS_SYSCALL(mkdirat),
	REDIS_SYSCALL(rename),
	REDIS_SYSCALL(renameat),
	REDIS_SYSCALL(renameat2),
	REDIS_SYSCALL(unlink),
	REDIS_SYSCALL(unlinkat),
	REDIS_SYSCALL(ftruncate),
	REDIS_SYSCALL(fallocate),
	REDIS_SYSCALL(fsync),
	REDIS_SYSCALL(fdatasync),
	REDIS_SYSCALL(flock),
	REDIS_SYSCALL(chmod),
	REDIS_SYSCALL(fchmod),
	REDIS_SYSCALL(fchmodat),
	REDIS_SYSCALL(umask),
	// INFO, SPOP, limits.
	REDIS_SYSCALL(uname),
	REDIS_SYSCALL(getrusage),
	REDIS_SYSCALL(times),
	REDIS_SYSCALL(sysinfo),
	REDIS_SYSCALL(getrlimit),
	REDIS_SYSCALL(prlimit64),
	REDIS_SYSCALL(getrandom),
};

/// Refused, but named in a report: what an attacker would try first.
constexpr std::array REFUSED_NAMED{
	REDIS_SYSCALL(socket),
	REDIS_SYSCALL(socketpair),
	REDIS_SYSCALL(connect),
	REDIS_SYSCALL(bind),
	REDIS_SYSCALL(listen),
	REDIS_SYSCALL(execve),
	REDIS_SYSCALL(execveat),
	REDIS_SYSCALL(vfork),
	REDIS_SYSCALL(ptrace),
	REDIS_SYSCALL(process_vm_readv),
	REDIS_SYSCALL(process_vm_writev),
	REDIS_SYSCALL(memfd_create),
	REDIS_SYSCALL(mount),
	REDIS_SYSCALL(chroot),
	REDIS_SYSCALL(unshare),
	REDIS_SYSCALL(setns),
	REDIS_SYSCALL(setuid),
	REDIS_SYSCALL(setgid),
	REDIS_SYSCALL(bpf),
	REDIS_SYSCALL(perf_event_open),
	REDIS_SYSCALL(io_uring_setup),
	REDIS_SYSCALL(userfaultfd),
	REDIS_SYSCALL(symlink),
	REDIS_SYSCALL(link),
	REDIS_SYSCALL(rmdir),
	REDIS_SYSCALL(chdir),
};

#undef REDIS_SYSCALL

// A BPF jump offset is 8 bits: each test jumps over the rest to the ALLOW.
static_assert(ALLOWED.size() < 255);

std::vector<sock_filter> build_filter() {
	std::vector<sock_filter> filter;
	filter.reserve(ALLOWED.size() + 8);
	// Another architecture's system call numbers mean other calls: kill.
	filter.push_back(
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)));
	filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
	filter.push_back(
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)));
	// The x32 ABI shares the architecture and sets this bit: kill.
	filter.push_back(
		BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, __X32_SYSCALL_BIT, 0, 1));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
	// Test i jumps over the n - 1 - i tests after it and the TRAP.
	const size_t n = ALLOWED.size();
	for (size_t i = 0; i < n; ++i)
		filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
								  static_cast<uint32_t>(ALLOWED[i].number),
								  static_cast<uint8_t>(n - i),
								  0));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP));
	filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
	return filter;
}

std::error_code last_error() { return {errno, std::generic_category()}; }

// ---- Landlock -------------------------------------------------------------

/// The file-system rights Landlock version \p abi knows: all of them are
/// handled, so what no rule allows is refused.
uint64_t handled_rights(long abi) {
	uint64_t rights = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
					  LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR |
					  LANDLOCK_ACCESS_FS_REMOVE_DIR |
					  LANDLOCK_ACCESS_FS_REMOVE_FILE |
					  LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
					  LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK |
					  LANDLOCK_ACCESS_FS_MAKE_FIFO |
					  LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM;
#ifdef LANDLOCK_ACCESS_FS_REFER
	if (abi >= 2) rights |= LANDLOCK_ACCESS_FS_REFER;
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
	if (abi >= 3) rights |= LANDLOCK_ACCESS_FS_TRUNCATE;
#endif
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
	if (abi >= 5) rights |= LANDLOCK_ACCESS_FS_IOCTL_DEV;
#endif
	return rights;
}

/// What a rule on a regular file may allow: only rights on the file itself.
constexpr uint64_t FILE_RIGHTS = LANDLOCK_ACCESS_FS_EXECUTE |
								 LANDLOCK_ACCESS_FS_WRITE_FILE |
								 LANDLOCK_ACCESS_FS_READ_FILE
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
								 | LANDLOCK_ACCESS_FS_TRUNCATE
#endif
	;

/// Read, create, replace (by rename in the same directory) and delete files.
constexpr uint64_t READ_WRITE_RIGHTS =
	LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE |
	LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
	LANDLOCK_ACCESS_FS_MAKE_REG
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
	| LANDLOCK_ACCESS_FS_TRUNCATE
#endif
	;

constexpr uint64_t READ_RIGHTS =
	LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;

/// A descriptor closed when it goes out of scope.
class Fd {
public:
	explicit Fd(int fd) noexcept : fd_(fd) {}
	Fd(const Fd &)            = delete;
	Fd &operator=(const Fd &) = delete;
	~Fd() {
		if (fd_ >= 0) ::close(fd_);
	}
	[[nodiscard]] int get() const noexcept { return fd_; }

private:
	int fd_;
};

/// Allow \p rights (within \p handled) beneath \p path in \p ruleset.
std::expected<void, SandboxError> allow(int ruleset,
										const std::filesystem::path &path,
										uint64_t rights, uint64_t handled) {
	const Fd target(::open(path.c_str(), O_PATH | O_CLOEXEC));
	if (target.get() < 0) {
		if (errno == ENOENT) return {}; // nothing there to allow
		REDIS_FAILED(SandboxError{.kind  = SandboxKind::CannotOpenPath,
								  .cause = last_error(),
								  .path  = path});
	}
	struct stat info {};
	if (::fstat(target.get(), &info) != 0)
		REDIS_FAILED(SandboxError{.kind  = SandboxKind::CannotOpenPath,
								  .cause = last_error(),
								  .path  = path});
	if (!S_ISDIR(info.st_mode)) rights &= FILE_RIGHTS;

	landlock_path_beneath_attr rule{};
	rule.allowed_access = rights & handled;
	rule.parent_fd      = target.get();
	if (::syscall(__NR_landlock_add_rule,
				  ruleset,
				  LANDLOCK_RULE_PATH_BENEATH,
				  &rule,
				  0) != 0)
		REDIS_FAILED(
			SandboxError{.kind = SandboxKind::Landlock, .cause = last_error()});
	return {};
}

bool is_landlock_missing(int error) noexcept {
	// ENOSYS: an older kernel; EOPNOTSUPP: built in but off; EPERM: a
	// container's own seccomp profile refusing the call.
	return error == ENOSYS || error == EOPNOTSUPP || error == EPERM;
}

} // namespace

std::expected<Confinement, SandboxError>
confine_paths(const SandboxPaths &paths) {
	const long abi = ::syscall(__NR_landlock_create_ruleset,
							   nullptr,
							   0,
							   LANDLOCK_CREATE_RULESET_VERSION);
	if (abi < 0) {
		if (is_landlock_missing(errno)) return Confinement::NotSupported;
		REDIS_FAILED(
			SandboxError{.kind = SandboxKind::Landlock, .cause = last_error()});
	}

	const uint64_t handled = handled_rights(abi);
	landlock_ruleset_attr attributes{};
	attributes.handled_access_fs = handled;
	const Fd ruleset(static_cast<int>(::syscall(__NR_landlock_create_ruleset,
												&attributes,
												sizeof attributes,
												0)));
	if (ruleset.get() < 0)
		REDIS_FAILED(
			SandboxError{.kind = SandboxKind::Landlock, .cause = last_error()});

	for (const auto &path : paths.read_write)
		REDIS_TRY(allow(ruleset.get(), path, READ_WRITE_RIGHTS, handled));
	for (const auto &path : paths.read_only)
		REDIS_TRY(allow(ruleset.get(), path, READ_RIGHTS, handled));

	// Required to restrict oneself without CAP_SYS_ADMIN; never undone.
	if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
		::syscall(__NR_landlock_restrict_self, ruleset.get(), 0) != 0)
		REDIS_FAILED(
			SandboxError{.kind = SandboxKind::Landlock, .cause = last_error()});
	return Confinement::Applied;
}

std::expected<Confinement, SandboxError> confine_syscalls() {
	std::vector<sock_filter> filter = build_filter();
	const sock_fprog program{
		.len    = static_cast<unsigned short>(filter.size()),
		.filter = filter.data(),
	};
	if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
		REDIS_FAILED(
			SandboxError{.kind = SandboxKind::Seccomp, .cause = last_error()});
	// TSYNC: every thread at once, the ones already running included. A
	// positive result is a thread that could not take the filter.
	const long installed = ::syscall(__NR_seccomp,
									 SECCOMP_SET_MODE_FILTER,
									 SECCOMP_FILTER_FLAG_TSYNC,
									 &program);
	if (installed != 0)
		REDIS_FAILED(SandboxError{
			.kind  = SandboxKind::Seccomp,
			.cause = installed > 0
						 ? std::make_error_code(std::errc::device_or_resource_busy)
						 : last_error()});
	return Confinement::Applied;
}

std::string_view syscall_name(long number) noexcept {
	for (const auto &table : {std::span<const Syscall>(ALLOWED),
							  std::span<const Syscall>(REFUSED_NAMED)})
		for (const Syscall &call : table)
			if (call.number == number) return call.name;
	return {};
}

} // namespace redis::os

#endif // REDIS_HAS_SANDBOX
