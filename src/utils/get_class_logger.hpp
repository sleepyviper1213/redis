#pragma once
#include <memory>

namespace spdlog {
class logger;
} // namespace spdlog

namespace redis {
std::shared_ptr<spdlog::logger> make_logger(const std::string &name);

#define CLASS_LOGGER(name)                                                     \
	inline static const auto logger_ = make_logger(#name);
} // namespace redis
