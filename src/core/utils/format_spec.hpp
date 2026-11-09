#pragma once

#include <fmt/base.h>

#include <string_view>

namespace redis {

/**
 * \brief Read the presentation character of a format specification, as the `e`
 * of `{:e}` or the `?` of `{:?}`, for a formatter that accepts nothing else.
 *
 * \p allowed lists the characters that may appear. The specification must be
 * that one character or empty: anything after it is an error, and so is a
 * character that is not allowed. Never reads past the end of the specification,
 * which an unfinished `{:e` can reach: the caller's fmt reports the missing `}`.
 *
 * Leaves \p ctx at the closing `}` (or at the end), so a formatter's parse() is
 *
 * @code
 *   presentation = redis::parse_presentation(ctx, "?e");
 *   return ctx.begin();
 * @endcode
 *
 * \return The character, or 0 for an empty specification.
 */
constexpr char parse_presentation(fmt::format_parse_context &ctx,
								  std::string_view allowed) {
	auto it        = ctx.begin();
	const auto end = ctx.end();

	char presentation = 0;
	if (it != end && *it != '}' && allowed.find(*it) != std::string_view::npos) {
		presentation = *it;
		++it;
	}
	if (it != end && *it != '}') fmt::report_error("invalid format specifier");

	ctx.advance_to(it);
	return presentation;
}

} // namespace redis
