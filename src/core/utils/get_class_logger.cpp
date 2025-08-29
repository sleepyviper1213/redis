#include "core/utils/get_class_logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace redis {
std::shared_ptr<spdlog::logger> make_logger(const std::string &name) {
	auto logger = spdlog::get(name);
	if (!logger) {
		logger = spdlog::stdout_color_mt(name);
		logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%n] [%^%l%$] %v");
	}
	return logger;
}
} // namespace redis
