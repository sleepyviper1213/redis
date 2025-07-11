#pragma once

#include <fmt/base.h>

#include <optional>
#include <string_view>


enum class SetCommand { SADD, SREM, SMEMBERS, SINTER, SCARD };

struct SetCommandUtil {
	static std::optional<SetCommand> fromString(std::string_view sv);
};

template <>
struct fmt::formatter<SetCommand> : formatter<string_view> {
	auto format(SetCommand c,
				format_context &ctx) const -> format_context::iterator;
};
