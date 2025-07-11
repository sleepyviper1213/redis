#pragma once
#include <fmt/base.h>
enum class ParseError { ARGS_PARSE_FAILED, TYPE_FAILED };

template <>
struct fmt::formatter<ParseError> : formatter<string_view> {
	format_context::iterator format(ParseError t, format_context &ctx) const;
};
