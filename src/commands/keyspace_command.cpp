#include "keyspace_command.hpp"

std::optional<KeyspaceCommand>
GenericCommandUtil::fromString(std::string_view sv) {
	using enum KeyspaceCommand;
	if (sv == "del") return DEL;
	if (sv == "expire") return EXPIRE;
	if (sv == "ttl") return TTL;
	if (sv == "keys") return KEYS;
	if (sv == "flushdb") return FLUSHDB;
	return std::nullopt;
}

auto fmt::formatter<KeyspaceCommand>::format(KeyspaceCommand c,
											 format_context &ctx) const
	-> format_context::iterator {
	using enum KeyspaceCommand;
	string_view name = "unknown";
	switch (c) {
	case DEL: name = "del"; break;
	case EXPIRE: name = "expire"; break;
	case TTL: name = "ttl"; break;
	case KEYS: name = "keys"; break;
	case FLUSHDB: name = "flushdb"; break;
	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}
