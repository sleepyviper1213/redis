#include "ttl_error.hpp"

#include <utility>

auto fmt::formatter<TTLError>::format(TTLError c, format_context &ctx) const
	-> format_context::iterator {
	string_view name = "unknown";
	switch (c) {
		using enum TTLError;
	case KEY_ITER_NOTFOUND: name = "KEY_ITER_NOTFOUND"; break;
	case TTL_NOT_FOUND: name = "TTL_NOT_FOUND"; break;
	case INVALID_ARGUMENT: name = "INVALID_ARGUMENT"; break;

	default: std::unreachable();
	}
	return formatter<string_view>::format(name, ctx);
}