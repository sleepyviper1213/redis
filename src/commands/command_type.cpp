#include "command_type.hpp"

std::optional<CommandType> CommandTypeUtil::fromString(std::string_view sv) {
	using enum CommandType;
	if (sv == "set") return SSET;
	if (sv == "get") return SGET;
	if (sv == "llen") return LLEN;
	if (sv == "lpush") return LPUSH;
	if (sv == "rpush") return RPUSH;
	if (sv == "lpop") return LPOP;
	if (sv == "rpop") return RPOP;
	if (sv == "lrange") return LRANGE;
	if (sv == "sadd") return SADD;
	if (sv == "srem") return SREM;
	if (sv == "smembers") return SMEMBERS;
	if (sv == "sinter") return SINTER;
	if (sv == "scard") return SCARD;
	if (sv == "del") return GDEL;
	if (sv == "keys") return GKEYS;
	if (sv == "flushdb") return GFLUSHDB;
	if (sv == "exit") return EXIT;
	if (sv == "save") return SNAPSHOT;
	if (sv == "restore") return RESTORE;

	return std::nullopt;
}

auto fmt::formatter<CommandType>::format(CommandType c,
										 format_context &ctx) const
	-> format_context::iterator {
	string_view name = "unknown";
	switch (c) {
		using enum CommandType;
	case SSET: name = "set"; break;
	case SGET: name = "get"; break;
	case LLEN: name = "llen"; break;
	case LPUSH: name = "lpush"; break;
	case RPUSH: name = "rpush"; break;
	case LPOP: name = "lpop"; break;
	case RPOP: name = "rpop"; break;
	case LRANGE: name = "lrange"; break;
	case SADD: name = "sadd"; break;
	case SREM: name = "srem"; break;
	case SMEMBERS: name = "smembers"; break;
	case SINTER: name = "sinter"; break;
	case SCARD: name = "scard"; break;
	case GDEL: name = "del"; break;
	case GKEYS: name = "keys"; break;
	case GFLUSHDB: name = "flushdb"; break;
	case EXIT: name = "exit"; break;
	case SNAPSHOT: name = "save"; break;
	case RESTORE: name = "restore"; break;
	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}
