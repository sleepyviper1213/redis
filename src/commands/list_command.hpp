#pragma once
#include <fmt/base.h>

#include <optional>
#include <string_view>
enum class ListCommand {
	LLEN,
	LPUSH,
	RPUSH,
	LPOP,
	RPOP,
	LRANGE,
};

template <>
struct fmt::formatter<ListCommand> : formatter<string_view> {
	format_context::iterator format(ListCommand t, format_context &ctx) const;
};

struct CommandTypeUtil {
	static std::optional<ListCommand> fromString(std::string_view sv);
};
