#include "parse_number.hpp"

#include "core/error/failed.hpp"

#include <fast_float/fast_float.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <system_error>

namespace redis {
namespace {
template <typename T>
std::expected<T, std::errc> parse_whole(std::string_view s) {
	T value{};
	auto [ptr, ec] =
		fast_float::from_chars(s.data(), s.data() + s.size(), value);
	// Trailing garbage is malformed whatever the prefix was: "12abc" reports
	// no error code, and an overflowing "99...9-" reports out of range
	// (TC-330).
	if (ptr != s.data() + s.size()) ec = std::errc::invalid_argument;
	if (ec != std::errc()) REDIS_FAILED(ec);
	return value;
}

/// Whether \p s spells its number with a leading zero ("007", "-0"), which
/// Redis' string2ll rejects: "0" alone is the only number starting with one.
bool has_leading_zero(std::string_view s) {
	const bool is_negative           = s.starts_with('-');
	const std::string_view digits = is_negative ? s.substr(1) : s;
	return digits.starts_with('0') && (digits.size() > 1 || is_negative);
}
} // namespace

std::expected<int64_t, std::errc> parse_integer(std::string_view s) {
	if (has_leading_zero(s)) REDIS_FAILED(std::errc::invalid_argument);
	return parse_whole<int64_t>(s);
}

std::expected<double, std::errc> parse_double(std::string_view s) {
	return parse_whole<double>(s);
}

} // namespace redis
