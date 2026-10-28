#include "core/logging/channels.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <memory>

namespace redis::logging {
namespace {

/// @brief The name each channel prints under, in enumerator order.
constexpr std::array<const char *, 4> CHANNEL_NAMES{
	"server",
	"net",
	"commands",
	"persistence",
};

[[nodiscard]] constexpr std::size_t index_of(channel which) noexcept {
	return static_cast<std::size_t>(which);
}

/**
 * @brief The installed channels, or empty until one is asked for.
 *
 * Function-local so there is no ordering question at start-up, and
 * @c shared_ptr so the sinks a channel writes to cannot be destroyed while a
 * channel still points at them.
 */
[[nodiscard]] std::array<std::shared_ptr<spdlog::logger>,
						 CHANNEL_NAMES.size()> &
slots() noexcept {
	static std::array<std::shared_ptr<spdlog::logger>, CHANNEL_NAMES.size()>
		installed;
	return installed;
}

/// @brief A clone of the current default logger under @p name - same sinks,
///        same level, same pattern - registered so `spdlog::get` finds it too.
[[nodiscard]] std::shared_ptr<spdlog::logger> clone_default(const char *name) {
	auto cloned = spdlog::default_logger()->clone(name);
	// Dropped first so re-installing over an earlier default (a second `init`,
	// or a test replacing the logger) is not an already-registered error.
	spdlog::drop(name);
	try {
		spdlog::register_logger(cloned);
	} catch (const spdlog::spdlog_ex &) { // NOLINT(bugprone-empty-catch)
		// Registration is a convenience for `spdlog::get`; the reference this
		// file hands out does not depend on it, and there is nothing to report
		// the failure *with*: the reporting channel is what just failed.
	}
	return cloned;
}

} // namespace

void install_channels() {
	auto &installed = slots();
	for (std::size_t i = 0; i < CHANNEL_NAMES.size(); ++i)
		installed[i] = clone_default(CHANNEL_NAMES[i]);
}

spdlog::logger &logger_for(channel which) noexcept {
	auto &installed      = slots();
	const std::size_t at = index_of(which);
	// A `channel` cast from an out-of-range integer would index past both
	// arrays. Guarded rather than asserted because this is the function
	// everything logs *through*.
	if (at >= CHANNEL_NAMES.size()) return *spdlog::default_logger();
	// Lazily, on the first miss: a process that never called `init` still
	// gets a working channel over spdlog's own default sinks.
	if (installed[at] == nullptr) {
		try {
			installed[at] = clone_default(CHANNEL_NAMES[at]);
		} catch (...) {
			return *spdlog::default_logger();
		}
	}
	return *installed[at];
}

} // namespace redis::logging
