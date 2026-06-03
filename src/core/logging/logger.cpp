#include "core/logging/logger.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace redis {
void Logger::init(const std::string &file, spdlog::level::level_enum level) {
	// Create sinks once
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_sink->set_level(level);

	auto file_sink =
		std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
	file_sink->set_level(level);

	sinks_     = {console_sink, file_sink};
	log_level_ = level;
}

std::shared_ptr<spdlog::logger>
Logger::get(const std::string &class_name) const {
	auto logger = spdlog::get(class_name);
	if (logger) return logger;
	if (sinks_.empty()) {
		throw std::runtime_error(
			"Logger not initialized. Call Logger::init() in main().");
	}
	// Create a new logger with the 2 sinks
	logger = std::make_shared<spdlog::logger>(class_name,
											  sinks_.begin(),
											  sinks_.end());
	logger->set_level(log_level_);
	logger->flush_on(spdlog::level::level_enum::info);
	spdlog::register_logger(logger);

	return logger;
}
} // namespace redis
