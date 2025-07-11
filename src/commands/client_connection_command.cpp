#include "client_connection_command.hpp"


#include <utility>


auto fmt::formatter<ClientConnectionCommand>::format(ClientConnectionCommand c,
													 format_context &ctx) const
	-> format_context::iterator {
	using enum ClientConnectionCommand;
	string_view name = "unknown";
	switch (c) {
	case QUIT: name = "quit"; break;
	case ECHO: name = "ECHO"; break;
	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}
