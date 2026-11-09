#include "channels.hpp"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace redis::logging {
namespace {

/**
 * @brief The installed channels.
 *
 * Function-local so there is no ordering question at start-up, and
 * @c shared_ptr so the sinks a channel writes to cannot be destroyed while a
 * channel still points at them.
 */
[[nodiscard]] std::vector<std::shared_ptr<spdlog::logger>> &slots() noexcept {
	static std::vector<std::shared_ptr<spdlog::logger>> installed;
	return installed;
}

/// @brief A clone of the current default logger under @p name - same sinks,
///        same level, same pattern - registered so `spdlog::get` finds it too.
[[nodiscard]] std::shared_ptr<spdlog::logger>
clone_default(const std::string &name) {
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

void install_channels(std::span<const std::string> names) {
	std::vector<std::shared_ptr<spdlog::logger>> fresh;
	fresh.reserve(names.size());
	for (const auto &name : names) fresh.push_back(clone_default(name));
	slots() = std::move(fresh);
}

std::size_t channel_count() noexcept { return slots().size(); }

spdlog::logger &logger_at(std::size_t index) noexcept {
	const auto &installed = slots();
	// Guarded rather than asserted because this is the function everything
	// logs *through*.
	if (index >= installed.size() || installed[index] == nullptr)
		return *spdlog::default_logger();
	return *installed[index];
}

} // namespace redis::logging
