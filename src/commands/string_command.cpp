#include "string_command.hpp"

std::optional<StringCommand> StringCommandUtil::fromString(std::string_view sv) {
    using enum StringCommand;
    if (sv == "set") return SSET;
    if (sv == "get") return SGET;
    return std::nullopt;
}

auto fmt::formatter<StringCommand>::format(StringCommand c, format_context& ctx) const
    -> format_context::iterator {
    using enum StringCommand;
    std::string_view name = "unknown";
    switch (c) {
        case SSET: name = "set"; break;
        case SGET: name = "get"; break;
        default: std::unreachable();
    }
    return formatter<std::string_view>::format(name, ctx);
}
