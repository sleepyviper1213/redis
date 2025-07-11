#include "parse_error.hpp"

#include <utility>

auto fmt::formatter<ParseError>::format(ParseError c, format_context &ctx) const
	-> format_context::iterator {
	string_view name = "unknown";
	switch (c) {
		using enum ParseError;
	case ARGS_PARSE_FAILED: name = "ARGS_PARSE_FAILED"; break;
	case TYPE_FAILED: name = "TYPE_FAILED"; break;

	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}