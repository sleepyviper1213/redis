#include "crash.hpp"

#include "current_command.hpp"
#include "core/os/child_process.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h> // NOLINT(modernize-deprecated-headers): sigaction
#include <unistd.h>

#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define REDIS_HAVE_BACKTRACE 1
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>

// Everything reachable from the signal handler must be async-signal-safe:
// no allocation, no stdio, no locks, so no logger and no fmt. Output goes
// through write(2), numbers through the small formatter below.
namespace redis::diagnostics {
namespace {

/// The log file, opened at install time: open(2) is safe in a handler, but a
/// path string is not something to rely on while the heap may be corrupt.
int log_fd = -1;

/// Set by the first fatal signal; a second one (a fault inside the report)
/// skips straight to the default action instead of recursing.
std::atomic<bool> is_reporting{false};

constexpr std::array FATAL_SIGNALS{SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

/// How long the report may take before on_report_timeout() ends the process.
constexpr unsigned REPORT_TIMEOUT_SECONDS = 3;

/// Test seam for TC-301: set at install time from REDIS_TEST_CRASH_REPORT_HANG,
/// it makes the report stop after its first line, as if a lock were held.
/// Not a documented option.
bool is_stalling_report = false;

/// A crash on a thread that has run out of stack needs another stack to
/// report from. Covers the thread that installed the handler (main).
constexpr std::size_t ALT_STACK_SIZE = 64 * 1024;
alignas(16) std::array<std::byte, ALT_STACK_SIZE> alt_stack;

/// Bounds of the installing thread's stack (DX-84); 0 where unknown.
std::uintptr_t stack_low = 0;

/// How far below the stack's lowest address a fault still counts as running
/// off it: the guard gap, and a large frame that jumps over part of it.
constexpr std::uintptr_t OVERFLOW_WINDOW = 256 * 1024;

/// Test seam for TC-302: set at install time from
/// REDIS_TEST_CRASH_STACK_OVERFLOW, it makes install_crash_handler() recurse
/// until the stack is gone. Not a documented option.
bool is_overflowing_stack = false;

void record_stack_bounds() noexcept {
#if defined(__linux__)
	pthread_attr_t attr;
	if (::pthread_getattr_np(::pthread_self(), &attr) != 0) return;
	void *low = nullptr;
	std::size_t size = 0;
	if (::pthread_attr_getstack(&attr, &low, &size) == 0)
		stack_low = reinterpret_cast<std::uintptr_t>(low);
	::pthread_attr_destroy(&attr);
#elif defined(__APPLE__)
	// macOS gives the top of the stack, which grows down from it.
	const auto top =
		reinterpret_cast<std::uintptr_t>(::pthread_get_stackaddr_np(::pthread_self()));
	const std::size_t size = ::pthread_get_stacksize_np(::pthread_self());
	if (top > size) stack_low = top - size;
#endif
}

/// Whether \p fault_address lies just below, or at the edge of, the recorded
/// stack: a SIGSEGV there is an overflow, not a wild pointer.
bool is_stack_overflow(std::uintptr_t fault_address) noexcept {
	return stack_low != 0 && fault_address < stack_low + 4096 &&
		   fault_address + OVERFLOW_WINDOW >= stack_low;
}

/// Never returns normally; the volatile buffer and the work after the call
/// keep the compiler from turning the recursion into a loop.
[[gnu::noinline]] int recurse_forever(int depth) {
	volatile char buffer[1024];
	buffer[0] = static_cast<char>(depth);
	const int result = recurse_forever(depth + 1);
	buffer[1] = static_cast<char>(result);
	return buffer[0] + buffer[1];
}

void write_fd(int fd, std::string_view text) noexcept {
	while (fd >= 0 && !text.empty()) {
		const auto n = ::write(fd, text.data(), text.size());
		if (n <= 0) return; // nothing more can be done about it
		text.remove_prefix(static_cast<std::size_t>(n));
	}
}

void out(std::string_view text) noexcept {
	write_fd(STDERR_FILENO, text);
	write_fd(log_fd, text);
}

/// \p value in \p base, without allocating.
void out_number(std::uintmax_t value, unsigned base = 10) noexcept {
	std::array<char, 32> buf{};
	auto pos = buf.size();
	do {
		buf[--pos] = "0123456789abcdef"[value % base];
		value /= base;
	} while (value != 0 && pos > 0);
	if (base == 16) out("0x");
	out({buf.data() + pos, buf.size() - pos});
}

std::string_view signal_name(int sig) noexcept {
	switch (sig) {
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS: return "SIGBUS";
	case SIGFPE: return "SIGFPE";
	case SIGILL: return "SIGILL";
	case SIGABRT: return "SIGABRT";
	default: return "signal";
	}
}

void report_command() noexcept {
	const auto cmd = current_command();
	out("Current command: ");
	if (cmd.name.empty()) {
		out("(none)\n");
		return;
	}
	out(cmd.name);
	out(" (");
	out_number(cmd.argc);
	out(" args)\n");
}

void report_backtrace() noexcept {
#ifdef REDIS_HAVE_BACKTRACE
	out("\n------ STACK TRACE ------\n");
	std::array<void *, 64> frames{};
	const int depth = ::backtrace(frames.data(), static_cast<int>(frames.size()));
	::backtrace_symbols_fd(frames.data(), depth, STDERR_FILENO);
	if (log_fd >= 0) ::backtrace_symbols_fd(frames.data(), depth, log_fd);
#else
	out("\n(no stack trace on this platform; use the core dump)\n");
#endif
}

/// The report hung (a lock held by the crashed thread, a corrupt heap behind
/// a libc call): give up on it and die with a core dump, which is what a
/// crash with no report would have done.
void on_report_timeout(int /*sig*/) {
	::signal(SIGABRT, SIG_DFL);
	::abort();
}

void on_fatal_signal(int sig, siginfo_t *info, void * /*context*/) {
	if (!is_reporting.exchange(true)) {
		::alarm(REPORT_TIMEOUT_SECONDS);
		out("\n\n=== REDIS BUG REPORT START: Cut & paste starting from "
			"here ===\n");
		if (is_stalling_report) {
			for (;;) ::pause(); // only SIGALRM gets us out
		}
		out("Crashed by signal: ");
		out(signal_name(sig));
		out(" (");
		out_number(static_cast<unsigned>(sig));
		out(")\n");
		if ((sig == SIGSEGV || sig == SIGBUS) && info != nullptr) {
			const auto fault = reinterpret_cast<std::uintptr_t>(info->si_addr);
			out("Fault address: ");
			out_number(fault, 16);
			out("\n");
			// DX-84: a fault at the end of the stack is the stack running out,
			// not a wild pointer.
			if (is_stack_overflow(fault))
				out("Stack overflow: the fault is at the end of the main "
					"thread's stack\n");
		}
		out("Pid: ");
		out_number(static_cast<std::uintmax_t>(::getpid()));
		out("\n");
		report_command();
		report_backtrace();
		out("\n=== REDIS BUG REPORT END. Make sure to include from START "
			"to END. ===\n\n");
		if (log_fd >= 0) ::fsync(log_fd);
	}
	// SA_RESETHAND has restored the default action, and SA_NODEFER lets it be
	// delivered now: terminate, with a core dump where enabled.
	::raise(sig);
}

/// Not in a signal handler, but the process is about to abort: report the
/// exception, then let SIGABRT write the rest of the report.
[[noreturn]] void on_terminate() noexcept {
	out("\nUncaught exception");
	if (const auto current = std::current_exception()) {
		try {
			std::rethrow_exception(current);
		} catch (const std::exception &e) {
			out(": ");
			out(e.what());
		} catch (...) { out(" of an unknown type"); }
	}
	out("\n");
	std::abort();
}

} // namespace

void install_crash_handler(const std::filesystem::path &log_file) {
	is_stalling_report = std::getenv("REDIS_TEST_CRASH_REPORT_HANG") != nullptr; // NOLINT(concurrency-mt-unsafe): read before any thread starts

	if (!log_file.empty())
		log_fd = ::open(log_file.c_str(),
						O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC,
						0644);
	// A BGSAVE child closes what it inherited; it must keep this one, or its
	// crash report would go nowhere, or into whatever file reused the number
	// (the snapshot it is writing).
	os::keep_open_in_children(log_fd);

#ifdef REDIS_HAVE_BACKTRACE
	// The first call loads the unwinder, which allocates: do it now, not in
	// the handler.
	std::array<void *, 1> warm_up{};
	(void)::backtrace(warm_up.data(), 1);
#endif

	stack_t stack{};
	stack.ss_sp   = alt_stack.data();
	stack.ss_size = alt_stack.size();
	::sigaltstack(&stack, nullptr);

	struct sigaction action {};
	action.sa_sigaction = on_fatal_signal;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER | SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	for (const int sig : FATAL_SIGNALS) ::sigaction(sig, &action, nullptr);

	struct sigaction timeout {};
	timeout.sa_handler = on_report_timeout;
	timeout.sa_flags   = SA_RESETHAND | SA_ONSTACK;
	sigemptyset(&timeout.sa_mask);
	::sigaction(SIGALRM, &timeout, nullptr);

	std::set_terminate(on_terminate);

	record_stack_bounds();
	is_overflowing_stack =
		std::getenv("REDIS_TEST_CRASH_STACK_OVERFLOW") != nullptr; // NOLINT(concurrency-mt-unsafe): read before any thread starts
	if (is_overflowing_stack) recurse_forever(0);
}

} // namespace redis::diagnostics
