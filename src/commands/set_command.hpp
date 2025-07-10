#pragma once

#include <optional>
#include <string_view>
#include <fmt/core.h>

enum class SetCommand {
    SADD,
    SREM,
    SMEMBERS,
    SINTER,
    SCARD
};

struct SetCommandUtil {
    static std::optional<SetCommand> fromString(std::string_view sv);
};

template <>
struct fmt::formatter<SetCommand> {
    template <typename FormatContext>
    auto format(SetCommand c, FormatContext& ctx) const;
};
