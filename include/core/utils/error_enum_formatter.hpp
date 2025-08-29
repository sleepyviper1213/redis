#pragma once
// clang-format off
#include <cstdlib>
#include <fmt/format.h>
// clang-format on
#include <magic_enum/magic_enum.hpp>

#include <type_traits>

#define ENUM_DEBUG_FORMATTER(type)                                             \
	static_assert(std::is_scoped_enum_v<type>);                                \
	template <>                                                                \
	struct fmt::formatter<type> : formatter<string_view> {                     \
		constexpr auto format(type c, format_context &ctx) const               \
			-> format_context::iterator {                                      \
			return formatter<string_view>::format(magic_enum::enum_name(c),    \
												  ctx);                        \
		}                                                                      \
	};


/**
 * \brief Formatter specialization for enum with fmtlib.
 *
 * Supported presentation types:
 * - '?' debug format
 * - 'e' integer reply in RESP format
 * - none: same as 'e'
 */
#define ENUM_DISPLAY_DEBUG_FORMATTER(type)                                     \
	static_assert(std::is_scoped_enum_v<type>);                                \
                                                                               \
	template <>                                                                \
	struct fmt::formatter<type> : formatter<string_view> {                     \
		char presentation = 0;                                                 \
                                                                               \
		constexpr auto parse(format_parse_context &ctx)                        \
			-> format_parse_context::iterator {                                \
			auto it = ctx.begin(), end = ctx.end();                            \
			if (it == end) return it;                                          \
			if (*it == '?' || *it == 'e') presentation = *it++;                \
			if (*it != '}') report_error("invalid format specifier");          \
			return it;                                                         \
		}                                                                      \
                                                                               \
		constexpr auto format(type c, format_context &ctx) const               \
			-> format_context::iterator {                                      \
			string_view name;                                                  \
			switch (presentation) {                                            \
			case '?': name = magic_enum::enum_name(c); break;                  \
			case 'e':                                                          \
			case 0: name = as_error_string(c); break;                          \
			default: std::unreachable();                                       \
			}                                                                  \
			return formatter<string_view>::format(name, ctx);                  \
		}                                                                      \
	};
\
