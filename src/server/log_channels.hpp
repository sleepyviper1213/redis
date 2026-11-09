#pragma once
// What the server's log channels are. The mechanism lives in
// core/logging/channels.hpp; this is the list.

#include "core/logging.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace redis::logging {

/**
 * @brief What a line is about.
 *
 * Named for the subject rather than the module path, because that is what a
 * reader is scanning for. The order is the position in settings::channels.
 */
enum class channel : uint8_t {
	server,      ///< startup, shutdown, signals, the periodic cron
	net,         ///< connections: accept, disconnect, protocol errors
	commands,    ///< requests and replies (trace level)
	persistence, ///< snapshot loads, SAVE and BGSAVE
};

/// Every channel, for code that must reach all of them.
inline constexpr std::array ALL_CHANNELS{
	channel::server,
	channel::net,
	channel::commands,
	channel::persistence,
};

/// The name each channel prints under, in enumerator order. Put these in
/// settings::channels before init().
[[nodiscard]] inline std::vector<std::string> channel_names() {
	return {"server", "net", "commands", "persistence"};
}

/// The logger for \p which. Cheap; do not keep the reference across init().
[[nodiscard]] inline spdlog::logger &logger_for(channel which) noexcept {
	return logger_at(static_cast<std::size_t>(which));
}

} // namespace redis::logging
