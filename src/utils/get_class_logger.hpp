#pragma once
#include <spdlog/logger.h>

#include <memory>

namespace redis {
std::shared_ptr<spdlog::logger> make_logger(const std::string &name);

#define CLASS_LOGGER(name)                                                     \
	inline static const auto logger_ = make_logger(#name);
} // namespace redis
