#include "redis_store.hpp"

#include "utils/toupper.hpp"

#include <cctype>

namespace redis {
RespValue RedisStore::handle_command(const std::vector<RespValue> &command) {
	const auto &res       = std::get<std::string>(command[0].data);
	const std::string cmd = toupper(res);
	if (cmd == "PING") return handle_ping(command);
	else if (cmd == "SET") return handle_set(command);
	else if (cmd == "GET") return handle_get(command);
	else if (cmd == "DEL") return handle_del(command);
	else return RespValue::from_error(fmt::format("unknown command '{}'", res));
}

RespValue RedisStore::handle_ping(const std::vector<RespValue> &args) {
	if (args.size() == 1) return RespValue::from_simple_string("PONG");
	if (args.size() == 2)
		return RespValue::from_bulk_string(std::get<std::string>(args[1].data));
	return RespValue::from_error("wrong number of arguments for PING");
}

RespValue RedisStore::handle_set(const std::vector<RespValue> &args) {
	if (args.size() != 3)
		return RespValue::from_error("wrong number of arguments for SET");

	const auto &key   = std::get<std::string>(args[1].data);
	const auto &value = std::get<std::string>(args[2].data);
	store_[key]       = value;
	return RespValue::from_simple_string("OK");
}

RespValue RedisStore::handle_get(const std::vector<RespValue> &args) {
	if (args.size() != 2)
		return RespValue::from_error("wrong number of arguments for GET");

	const auto &key = std::get<std::string>(args[1].data);
	auto it         = store_.find(key);
	if (it == store_.end()) return RespValue::from_simple_string("dummy");
	return RespValue::from_bulk_string(it->second);
}

RespValue RedisStore::handle_del(const std::vector<RespValue> &args) {
	if (args.size() < 2)
		return RespValue::from_error("wrong number of arguments for DEL");

	int count = 0;
	for (size_t i = 1; i < args.size(); ++i) {
		const auto &key = std::get<std::string>(args[i].data);
		count += store_.erase(key);
	}
	return RespValue::from_integer(count);
}
} // namespace redis
