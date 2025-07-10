#pragma once

#include <optional>
#include <string_view>
#include <fmt/core.h>

enum class StringCommand {
    SSET,
    SGET
};

struct StringCommandUtil {
    static std::optional<StringCommand> fromString(std::string_view sv);
};

template <>
struct fmt::formatter<StringCommand> {
    template <typename FormatContext>
    auto format(StringCommand c, FormatContext& ctx) const;
};
