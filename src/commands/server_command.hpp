#pragma once

#include <fmt/base.h>

#include <optional>
#include <string_view>


enum class ServerCommand { EXIT, SNAPSHOT, RESTORE };

struct ServerCommandUtil {
	static std::optional<ServerCommand> fromString(std::string_view sv);
};

template <>
struct fmt::formatter<ServerCommand> : formatter<string_view> {
	auto format(ServerCommand c,
				format_context &ctx) const -> format_context::iterator;
};
