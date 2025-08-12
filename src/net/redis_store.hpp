#pragma once

#include "resp/value.hpp"

#include <string>
#include <unordered_map>

namespace redis {

class RedisStore {
public:
	resp::Value handle_command(const resp::Value::Array &command);

private:
	std::unordered_map<std::string, std::string> store_;

	resp::Value handle_ping(const resp::Value::Array &args) const;

	resp::Value handle_set(const resp::Value::Array &args);

	resp::Value handle_get(const resp::Value::Array &args) const;

	resp::Value handle_del(const resp::Value::Array &args);
};
} // namespace redis
