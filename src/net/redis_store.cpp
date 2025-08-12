#include "redis_store.hpp"

#include "resp/value.hpp"
#include "utils/toupper.hpp"

#include <fmt/format.h>

#include <cctype>

using resp::Value;

namespace redis {
Value RedisStore::handle_command(const Value::Array &command) {
	const auto &res       = command[0].as_string();
	const std::string cmd = toupper(res);
	if (cmd == "PING") return handle_ping(command);
	else if (cmd == "SET") return handle_set(command);
	else if (cmd == "GET") return handle_get(command);
	else if (cmd == "DEL") return handle_del(command);
	else
		return Value::from_simple_error(
			fmt::format("unknown command '{}'", res));
}

Value RedisStore::handle_ping(const Value::Array &args) const {
	if (args.size() == 1) return Value::from_simple_string("PONG");
	if (args.size() == 2) return Value::from_bulk_string(args[1].as_string());
	return Value::from_simple_error("wrong number of arguments for PING");
}

Value RedisStore::handle_set(const Value::Array &args) {
	if (args.size() != 3)
		return Value::from_simple_error("wrong number of arguments for SET");

	const auto &key   = args[1].as_string();
	const auto &value = args[2].as_string();
	store_[key]       = value;
	return Value::from_simple_string("OK");
}

Value RedisStore::handle_get(const Value::Array &args) const {
	if (args.size() != 2)
		return Value::from_simple_error("wrong number of arguments for GET");

	const auto &key = args[1].as_string();
	auto it         = store_.find(key);
	if (it == store_.end()) return Value::from_simple_string("dummy");
	return Value::from_bulk_string(it->second);
}

Value RedisStore::handle_del(const Value::Array &args) {
	if (args.size() < 2)
		return Value::from_simple_error("wrong number of arguments for DEL");

	int64_t count = 0;
	for (size_t i = 1; i < args.size(); ++i) {
		const auto &key = args[1].as_string();
		count += store_.erase(key);
	}
	return Value::from_integer(count);
}
} // namespace redis
