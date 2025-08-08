#pragma once
#include "resp_parser.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace redis {
class RedisStore {
public:
	RespValue handle_command(const std::vector<RespValue> &command);

private:
	std::unordered_map<std::string, std::string> store_;

	RespValue handle_ping(const std::vector<RespValue> &args);

	RespValue handle_set(const std::vector<RespValue> &args);

	RespValue handle_get(const std::vector<RespValue> &args);

	RespValue handle_del(const std::vector<RespValue> &args);
};
} // namespace redis
