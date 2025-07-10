#pragma once

#include <optional>
#include <string_view>
#include <fmt/core.h>

enum class ServerCommand {
    EXIT,
    SNAPSHOT,
    RESTORE
};

struct ServerCommandUtil {
    static std::optional<ServerCommand> fromString(std::string_view sv);
};

template <>
struct fmt::formatter<ServerCommand> {
    template <typename FormatContext>
    auto format(ServerCommand c, FormatContext& ctx) const;
};
