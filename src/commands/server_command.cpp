#include "server_command.hpp"

std::optional<ServerCommand> ServerCommandUtil::fromString(std::string_view sv) {
    using enum ServerCommand;
    if (sv == "exit") return EXIT;
    if (sv == "save") return SNAPSHOT;
    if (sv == "restore") return RESTORE;
    return std::nullopt;
}

auto fmt::formatter<ServerCommand>::format(ServerCommand c, format_context& ctx) const
    -> format_context::iterator {
    using enum ServerCommand;
    string_view name = "unknown";
    switch (c) {
        case EXIT: name = "exit"; break;
        case SNAPSHOT: name = "save"; break;
        case RESTORE: name = "restore"; break;
        default: std::unreachable();
    }
    return formatter<string_view>::format(name, ctx);
}
