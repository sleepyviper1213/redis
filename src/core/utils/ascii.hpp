#pragma once
#include <algorithm>
#include <string>
#include <string_view>

namespace redis {

/// ASCII-only lower-casing of one byte. Unlike std::tolower it does not depend
/// on the C locale, and leaves every byte of a multibyte character alone.
[[nodiscard]] constexpr char ascii_lower(char c) noexcept {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Equality ignoring ASCII case, without copying either side: command names and
/// options are bytes a client sent, not locale text.
[[nodiscard]] constexpr bool iequals(std::string_view lhs,
									 std::string_view rhs) noexcept {
	return std::ranges::equal(lhs, rhs, [](char left, char right) {
		return ascii_lower(left) == ascii_lower(right);
	});
}

/// \p text with its ASCII upper-case letters lower-cased and every other byte
/// kept, for a message that quotes a name. Allocates, so not for a hot path.
[[nodiscard]] inline std::string ascii_lowered(std::string_view text) {
	std::string lowered(text);
	std::ranges::transform(lowered, lowered.begin(), ascii_lower);
	return lowered;
}

} // namespace redis
