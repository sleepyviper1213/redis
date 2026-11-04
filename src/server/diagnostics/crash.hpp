#pragma once

#include <string>

// The crash report: what an operator reads after the server died.
//
// POSIX only. Windows builds are for development and get no handler.
namespace redis::diagnostics {

/**
 * \brief Install handlers that write a crash report when the process dies of
 * SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT (a failed assert included), or
 * of an uncaught exception (std::terminate).
 *
 * The report gives the signal and faulting address, the pid, the command
 * being executed (see current_command.hpp) and a stack trace. It is written
 * with write(2) to stderr and appended to \p log_file (if not empty), not
 * through the logger: an async logger's queue dies with the process, and
 * the report must not.
 *
 * After the report the signal's default action runs, so a core dump is still
 * produced where the system is set up for one (ulimit -c, core_pattern).
 *
 * Call once, from main, after logging is set up. The forked BGSAVE child
 * inherits the handlers, which is fine: its crash gets a report too.
 */
#ifdef _WIN32
inline void install_crash_handler(const std::string & /*log_file*/) {}
#else
void install_crash_handler(const std::string &log_file);
#endif

} // namespace redis::diagnostics
