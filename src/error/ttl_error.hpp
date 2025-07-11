#pragma once
#include <fmt/base.h>
enum class TTLError { KEY_ITER_NOTFOUND, TTL_NOT_FOUND, INVALID_ARGUMENT };

template <>
struct fmt::formatter<TTLError> : formatter<string_view> {
	format_context::iterator format(TTLError t, format_context &ctx) const;
};
