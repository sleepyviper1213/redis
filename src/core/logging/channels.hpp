#pragma once
// Named loggers, so a line says which part of a program it came from.
//
// One default logger means every line looks alike, and in a server where
// connections come and go, commands run and background saves finish all at
// once, "which of these is this" is the first question a reader has. A channel
// answers it in the pattern rather than in the message text.
//
// This file is only the mechanism: it knows channels by position and name, and
// nothing about what they are for. The program decides the list (see
// settings::channels) and gives its own names to the positions, typically with
// an enum.
//
// --- how they relate to the default logger ---------------------------------
//
// Each is a clone of it: same sinks, same level, same pattern, same backtrace
// depth, taken at `init` time. So `--log-level=trace` reaches every channel,
// `--log-file` collects every channel, and a deployment configures one thing.
// A channel asked for before `init` has run, or when no channels were
// configured, is the default logger itself, which keeps a test that never
// initialises logging working.
//
// --- do not cache the reference --------------------------------------------
//
// `init` replaces the channel loggers, so a reference kept from before it (in
// a member, or a static) may dangle. Call `logger_at` where you log.

#include <spdlog/logger.h>

#include <cstddef>
#include <span>
#include <string>

namespace redis::logging {

/**
 * @brief The logger for the channel at position @p index.
 *
 * @return Never null: the default logger if @p index is out of range or no
 *         channels are installed. Valid until the next @c init.
 *
 * @note Cheap: a pointer load, and spdlog's own level check is an atomic load.
 *       Not safe to call concurrently with @c init, which must run before the
 *       threads that log are started. @see lifecycle.hpp
 */
[[nodiscard]] spdlog::logger &logger_at(std::size_t index) noexcept;

/// How many channels are installed.
[[nodiscard]] std::size_t channel_count() noexcept;

/**
 * @brief Install a clone of the default logger for each of @p names, in order,
 *        replacing any installed before.
 *
 * Called by @c init with settings::channels, so a process that configures
 * logging normally never needs this. Exposed for a test that installs its own
 * default logger and wants the channels to follow it.
 */
void install_channels(std::span<const std::string> names);

} // namespace redis::logging
