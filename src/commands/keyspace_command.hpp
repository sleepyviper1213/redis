#pragma once

#include <fmt/core.h>

#include <optional>
#include <string_view>

enum class KeyspaceCommand {
	DEL,
	EXPIRE,
	TTL,
	KEYS,
	FLUSHDB,

};

struct GenericCommandUtil {
	static std::optional<KeyspaceCommand> fromString(std::string_view sv);
};

template <>
struct fmt::formatter<KeyspaceCommand> : formatter<string_view> {
	auto format(KeyspaceCommand c, format_context &ctx) const
		-> format_context::iterator;
};
