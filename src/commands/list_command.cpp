#include "list_command.hpp"

std::optional<ListCommand> CommandTypeUtil::fromString(std::string_view sv) {
	using enum ListCommand;
	if (sv == "llen") return LLEN;
	if (sv == "lpush") return LPUSH;
	if (sv == "rpush") return RPUSH;
	if (sv == "lpop") return LPOP;
	if (sv == "rpop") return RPOP;
	if (sv == "lrange") return LRANGE;

	return std::nullopt;
}

auto fmt::formatter<ListCommand>::format(ListCommand c,
										 format_context &ctx) const
	-> format_context::iterator {
	string_view name = "unknown";
	switch (c) {
		using enum ListCommand;
	case LLEN: name = "llen"; break;
	case LPUSH: name = "lpush"; break;
	case RPUSH: name = "rpush"; break;
	case LPOP: name = "lpop"; break;
	case RPOP: name = "rpop"; break;
	case LRANGE: name = "lrange"; break;
	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}
