#include "command_type.hpp"

auto fmt::formatter<CommandType>::format(
	CommandType c, format_context &ctx) const -> format_context::iterator {
	using enum CommandType;
	string_view name = "unknown";
	switch (c) {
	case DEL: name = "del"; break;
	case EXPIRE: name = "expire"; break;
	case TTL: name = "ttl"; break;
	case KEYS: name = "keys"; break;
	case FLUSHDB: name = "flushdb"; break;
	case LLEN: name = "llen"; break;
	case LPUSH: name = "lpush"; break;
	case RPUSH: name = "rpush"; break;
	case LPOP: name = "lpop"; break;
	case RPOP: name = "rpop"; break;
	case LRANGE: name = "lrange"; break;
	case EXIT: name = "exit"; break;
	case SNAPSHOT: name = "save"; break;
	case RESTORE: name = "restore"; break;
	case SADD: name = "sadd"; break;
	case SREM: name = "srem"; break;
	case SMEMBERS: name = "smembers"; break;
	case SINTER: name = "sinter"; break;
	case SCARD: name = "scard"; break;
	case SSET: name = "set"; break;
	case SGET: name = "get"; break;

	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}

std::optional<CommandType> CommandTypeUtil::fromString(std::string_view sv) {
	using enum CommandType;

	if (sv == "del") return DEL;
	if (sv == "expire") return EXPIRE;
	if (sv == "ttl") return TTL;
	if (sv == "keys") return KEYS;
	if (sv == "flushdb") return FLUSHDB;
	if (sv == "llen") return LLEN;
	if (sv == "lpush") return LPUSH;
	if (sv == "rpush") return RPUSH;
	if (sv == "lpop") return LPOP;
	if (sv == "rpop") return RPOP;
	if (sv == "lrange") return LRANGE;
	if (sv == "exit") return EXIT;
	if (sv == "save") return SNAPSHOT;
	if (sv == "restore") return RESTORE;
	if (sv == "sadd") return SADD;
	if (sv == "srem") return SREM;
	if (sv == "smembers") return SMEMBERS;
	if (sv == "sinter") return SINTER;
	if (sv == "scard") return SCARD;
	if (sv == "set") return SSET;
	if (sv == "get") return SGET;

	return std::nullopt;
}
