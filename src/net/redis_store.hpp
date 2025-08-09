#pragma once

#include "resp/value.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace redis {

class RedisStore {
public:
	resp::Value handle_command(const std::vector<resp::Value> &command);

private:
	std::unordered_map<std::string, std::string> store_;

	resp::Value handle_ping(const std::vector<resp::Value> &args);

	resp::Value handle_set(const std::vector<resp::Value> &args);

	resp::Value handle_get(const std::vector<resp::Value> &args);

	resp::Value handle_del(const std::vector<resp::Value> &args);
};
} // namespace redis
