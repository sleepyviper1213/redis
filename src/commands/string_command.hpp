#pragma once

#include <fmt/base.h>

#include <optional>
#include <string_view>


enum class StringCommand { SSET, SGET };

struct StringCommandUtil {
	static std::optional<StringCommand> fromString(std::string_view sv);
};

template <>
struct fmt::formatter<StringCommand> : formatter<string_view> {
	auto format(StringCommand c,
				format_context &ctx) const -> format_context::iterator;
};
