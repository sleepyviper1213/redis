#include "database.hpp"

#include "resp/value.hpp"
#include "utils/toupper.hpp"

#include <fmt/format.h>
#include <spdlog/logger.h>

#include <cctype>

using resp::Value;

namespace redis {

Value Database::handle_command(const Value &command) {
	logger_->info("Received command {:?}", command);

	const auto &args      = command.as_array();
	const auto &res       = args[0].as_string();
	const std::string cmd = toupper(res);
	if (cmd == "PING") return handle_ping(args);
	else if (cmd == "SET") return handle_set(args);
	else if (cmd == "GET") return handle_get(args);
	else if (cmd == "DEL") return handle_del(args);
	else
		return Value::from_simple_error(
			fmt::format("unknown command '{}'", res));
}

Value Database::handle_ping(const Value::Array &args) const {
	if (args.size() == 1) return Value::from_simple_string("PONG");
	if (args.size() == 2) return Value::from_bulk_string(args[1].as_string());
	return Value::from_simple_error("wrong number of arguments for PING");
}

Value Database::handle_set(const Value::Array &args) {
	if (args.size() != 3)
		return Value::from_simple_error("wrong number of arguments for SET");

	const auto &key   = args[1].as_string();
	const auto &value = args[2].as_string();
	store_[key]       = value;
	return Value::from_simple_string("OK");
}

Value Database::handle_get(const Value::Array &args) const {
	if (args.size() != 2)
		return Value::from_simple_error("wrong number of arguments for GET");

	const auto &key = args[1].as_string();
	auto it         = store_.find(key);
	if (it == store_.end()) return Value::from_simple_string("dummy");
	return Value::from_bulk_string(it->second);
}

Value Database::handle_del(const Value::Array &args) {
	if (args.size() < 2)
		return Value::from_simple_error("wrong number of arguments for DEL");

	int64_t count = 0;
	for (size_t i = 1; i < args.size(); ++i) {
		const auto &key = args[1].as_string();
		count += store_.erase(key);
	}
	return Value::from_integer(count);
}

void Database::save() const { logger_->info("DB saved"); }

} // namespace redis
