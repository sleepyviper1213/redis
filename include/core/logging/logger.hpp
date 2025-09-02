#pragma once
#include "core/utils/meyers_singleton.hpp"

#include <spdlog/common.h>
#include <spdlog/logger.h>

#include <memory>
#include <string>
#include <vector>

namespace redis {
class Logger : public global::MayersSingleton<Logger> {
public:
	void init(const std::string &file, spdlog::level::level_enum level);

	// Get a logger tied to a component (class name, subsystem, etc.)
	[[nodiscard]] std::shared_ptr<spdlog::logger>
	get(const std::string &name) const;

private:
	std::vector<spdlog::sink_ptr> sinks_;
	spdlog::level::level_enum log_level_;
};
} // namespace redis

#define CLASS_LOGGER(name)                                                     \
	std::shared_ptr<spdlog::logger> logger_ = Logger::get_instance().get(#name)
