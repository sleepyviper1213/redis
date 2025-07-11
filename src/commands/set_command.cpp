#include "set_command.hpp"

std::optional<SetCommand> SetCommandUtil::fromString(std::string_view sv) {
	using enum SetCommand;
	if (sv == "sadd") return SADD;
	if (sv == "srem") return SREM;
	if (sv == "smembers") return SMEMBERS;
	if (sv == "sinter") return SINTER;
	if (sv == "scard") return SCARD;
	return std::nullopt;
}

auto fmt::formatter<SetCommand>::format(SetCommand c, format_context &ctx) const
	-> format_context::iterator {
	using enum SetCommand;
	std::string_view name = "unknown";
	switch (c) {
	case SADD: name = "sadd"; break;
	case SREM: name = "srem"; break;
	case SMEMBERS: name = "smembers"; break;
	case SINTER: name = "sinter"; break;
	case SCARD: name = "scard"; break;
	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}
