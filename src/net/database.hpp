#pragma once

#include "resp/value.hpp"
#include "utils/get_class_logger.hpp"

#include <string>
#include <unordered_map>

namespace redis {

class Database {
public:
	resp::Value handle_command(const resp::Value &command);

	void save() const;

private:
	std::unordered_map<std::string, std::string> store_;
	CLASS_LOGGER(Database);

	resp::Value handle_ping(const resp::Value::Array &args) const;

	resp::Value handle_set(const resp::Value::Array &args);

	resp::Value handle_get(const resp::Value::Array &args) const;

	resp::Value handle_del(const resp::Value::Array &args);
};
} // namespace redis
