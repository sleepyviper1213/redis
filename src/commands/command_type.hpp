#pragma once
#include <fmt/base.h>

#include <optional>
#include <string_view>


enum class CommandType {

	DEL,
	EXPIRE,
	TTL,
	KEYS,
	FLUSHDB,
	LLEN,
	LPUSH,
	RPUSH,
	LPOP,
	RPOP,
	LRANGE,
	SADD,
	SREM,
	SMEMBERS,
	SINTER,
	SCARD,
	SSET,
	SGET,
	EXIT,
	SNAPSHOT,
	RESTORE
};

namespace CommandTypeUtil {
std::optional<CommandType> fromString(std::string_view sv);
} // namespace CommandTypeUtil

template <>
struct fmt::formatter<CommandType> : formatter<string_view> {
	auto format(CommandType c,
				format_context &ctx) const -> format_context::iterator;
};
