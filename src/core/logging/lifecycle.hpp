#pragma once

#include "core/logging/settings.hpp"

// Installing, draining, and tearing down the process-wide logger.
//
// No <spdlog/spdlog.h> here either: none of these declarations names a spdlog
// type, so a translation unit that only starts logging up need not compile the
// library that does the logging. Include core/logging.hpp to emit messages.
namespace redis::logging {

/**
 * @brief Install the process-wide default logger and its channels.
 *
 * Call once, early, before anything else logs - messages emitted beforehand go
 * to spdlog's own default logger and miss the file sink. Calling it again
 * replaces the default logger, which is what a test that wants a quiet run
 * needs.
 *
 * The console sink is @b stderr, not stdout: a log carries diagnostics, and
 * keeping it off stdout lets a caller redirect one without the other.
 *
 * @warning Once this has been called, nothing may log from the destructor of
 *          an object with static storage duration. Those run after @c main,
 *          when the installed logger and its sinks are already destroyed.
 *
 * @warning Not thread-safe against concurrent logging: call it before the
 *          threads that will log are started.
 */
void init(const settings &config);

/**
 * @brief Emit the held-back backtrace ring, then clear it. No-op when
 *        @c settings::backtrace was 0.
 *
 * Call it where a failure is being reported and the steps leading to it are
 * worth having.
 */
void dump_backtrace() noexcept;

/**
 * @brief Push every buffered message to its sinks, keeping the logger usable.
 *
 * @note Under @c settings::async - the default - this @em enqueues the flush
 *       rather than performing it. @c shutdown is the one that drains
 *       synchronously, by joining the thread pool.
 */
void flush() noexcept;

/**
 * @brief Flush every sink and drop the default logger.
 *
 * @warning After this, nothing may log again: @c spdlog::default_logger()
 *          returns null and the next log call dereferences it.
 *
 * @par Under settings::async this must be reached before main returns
 * An async logger owns a worker thread held by spdlog's registry, which is a
 * static. Left alive, it is joined during static destruction - on Windows
 * under the loader lock, which the worker needs to finish exiting, so the
 * process hangs after main returns. Joined from ordinary running code, as
 * here, it completes. @c guard does this at the end of its scope.
 */
void shutdown() noexcept;

} // namespace redis::logging
