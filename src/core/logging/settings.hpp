#pragma once

#include <cstddef>
#include <string>

// Configuration vocabulary - data only, no behaviour.
//
// Deliberately free of <spdlog/spdlog.h>: a translation unit that merely
// *describes* how the process should log (main, a test fixture) has no reason
// to compile the whole logging library to do it. Only code that actually emits
// messages needs that, and it gets it from core/logging.hpp.
namespace redis::logging {

/// @brief How the process should log.
struct settings {
	/// spdlog level name: trace | debug | info | warn | error | critical | off.
	/// An unrecognised name falls back to info with a warning rather than
	/// silently disabling logging.
	std::string level = "info";

	/// Logs always go to stderr; when this is non-empty they also go here.
	/// Empty disables the file sink.
	std::string log_file = "redis_server.log";

	std::string logger_name = "redis";

	/**
	 * @brief Emit one JSON object per line instead of the human-readable
	 *        pattern, for a log a machine ingests rather than a terminal
	 *        someone reads.
	 *
	 * Only chooses which @c spdlog::formatter the logger installs at @c init,
	 * so it is a one-time setup decision, not a per-call branch. Every field
	 * is escaped, including the message, so a payload containing a quote or a
	 * newline (a client's key, say) cannot break its line into two.
	 */
	bool structured = false;

	/**
	 * @brief Ring buffer of recent messages held back for @c dump_backtrace.
	 *        0 disables it.
	 *
	 * With this on, messages @em below the active level are still formatted
	 * and stored, so a later @c dump_backtrace can print the run-up to a
	 * failure at a detail the log itself never carried. That is also the cost:
	 * every sub-level call stops being nearly free and does real work. Useful
	 * on a cold path where the last N steps before an error matter.
	 */
	std::size_t backtrace = 0;

	/**
	 * @brief Hand each line to a background thread instead of formatting the
	 *        pattern and writing the sinks on the thread that logged it.
	 *
	 * The server runs every client on one event-loop thread, so a synchronous
	 * sink write is latency every connected client waits for. Asynchronously,
	 * that thread pays a formatted-buffer move and one enqueue.
	 *
	 * @note What this does @em not move is the message payload. spdlog formats
	 *       the caller's arguments before the queue - it has to, since they are
	 *       references that would dangle - so keeping the payload cheap is
	 *       still the caller's job.
	 *
	 * @warning A crash loses whatever is still queued. @c shutdown drains it,
	 * so an orderly exit loses nothing. Set this false to get synchronous
	 * behaviour back while debugging something that kills the process.
	 */
	bool async = true;

	/**
	 * @brief Lines the async queue holds before the overflow policy applies.
	 *
	 * When it fills, the oldest queued line is dropped rather than blocking
	 * the event loop: the queue fills when the server is busiest, and blocking
	 * there would stall every client on a diagnostic.
	 */
	std::size_t async_queue = 8192;
};

} // namespace redis::logging
