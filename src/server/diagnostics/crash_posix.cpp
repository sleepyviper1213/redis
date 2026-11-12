#include "crash.hpp"

#include "current_command.hpp"
#include "core/os/child_process.hpp"

#include <fcntl.h>
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
#include <string>
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
std::atomic<bool> reporting{false};

constexpr std::array FATAL_SIGNALS{SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};

/// A crash on a thread that has run out of stack needs another stack to
/// report from. Covers the thread that installed the handler (main).
constexpr std::size_t ALT_STACK_SIZE = 64 * 1024;
alignas(16) std::array<std::byte, ALT_STACK_SIZE> alt_stack;

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

void on_fatal_signal(int sig, siginfo_t *info, void * /*context*/) {
	if (!reporting.exchange(true)) {
		out("\n\n=== REDIS BUG REPORT START: Cut & paste starting from "
			"here ===\n");
		out("Crashed by signal: ");
		out(signal_name(sig));
		out(" (");
		out_number(static_cast<unsigned>(sig));
		out(")\n");
		if ((sig == SIGSEGV || sig == SIGBUS) && info != nullptr) {
			out("Fault address: ");
			out_number(reinterpret_cast<std::uintptr_t>(info->si_addr), 16);
			out("\n");
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

void install_crash_handler(const std::string &log_file) {
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

	std::set_terminate(on_terminate);
}

} // namespace redis::diagnostics
