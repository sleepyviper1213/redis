#pragma once
#include <fmt/base.h>
enum class ClientConnectionCommand {
	QUIT,
	ECHO,
};

template <>
struct fmt::formatter<ClientConnectionCommand> : formatter<string_view> {
	format_context::iterator format(ClientConnectionCommand t,
									format_context &ctx) const;
};
