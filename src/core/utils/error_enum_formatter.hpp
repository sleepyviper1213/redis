#pragma once
// clang-format off
#include <cstdlib>
#include <fmt/format.h>
// clang-format on
#include "core/utils/enum_name.hpp"

#ifndef ENUM_DEBUG_FORMATTER
#define ENUM_DEBUG_FORMATTER(type)                                             \
	static_assert(std::is_scoped_enum_v<type>);                                \
	template <>                                                                \
	struct fmt::formatter<type> : formatter<string_view> {                     \
		constexpr auto format(type c, format_context &ctx) const               \
			-> format_context::iterator {                                      \
			return formatter<string_view>::format(redis::enum_name(c), ctx);   \
		}                                                                      \
	};
#endif