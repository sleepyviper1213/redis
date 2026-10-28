#pragma once
// Named loggers, so a line says which part of the server it came from.
//
// One default logger means every line looks alike, and in a server where
// connections come and go, commands run and background saves finish all at
// once, "which of these is this" is the first question a reader has. A channel
// answers it in the pattern rather than in the message text.
//
// --- how they relate to the default logger ---------------------------------
//
// Each is a clone of it: same sinks, same level, same pattern, same backtrace
// depth, taken at `init` time. So `--log-level=trace` reaches every channel,
// `--log-file` collects every channel, and a deployment configures one thing.
// A channel asked for before `init` has run clones whatever spdlog's own
// default logger is, which keeps a test that never initialises logging working.
//
// --- do not cache the reference --------------------------------------------
//
// `init` replaces the channel loggers, so a reference kept from before it (in
// a member, or a static) may dangle. Call `logger_for` where you log.

#include <spdlog/logger.h>

#include <cstdint>

namespace redis::logging {

/**
 * @brief What a line is about.
 *
 * Named for the subject rather than the module path, because that is what a
 * reader is scanning for.
 */
enum class channel : std::uint8_t {
	server,      ///< startup, shutdown, signals, the periodic cron
	net,         ///< connections: accept, disconnect, protocol errors
	commands,    ///< requests and replies (trace level)
	persistence, ///< snapshot loads, SAVE and BGSAVE
};

/**
 * @brief The logger for @p which.
 *
 * @return Never null: if no channel has been installed it clones the current
 *         default logger on first use. Valid until the next @c init.
 *
 * @note Cheap: a pointer load, and spdlog's own level check is an atomic load.
 *       Not safe to call concurrently with @c init, which must run before the
 *       threads that log are started. @see lifecycle.hpp
 */
[[nodiscard]] spdlog::logger &logger_for(channel which) noexcept;

/**
 * @brief Install a clone of the default logger for every channel.
 *
 * Called by @c init, so a process that configures logging normally never
 * needs this. Exposed for a test that installs its own default logger and
 * wants the channels to follow it.
 */
void install_channels();

} // namespace redis::logging
