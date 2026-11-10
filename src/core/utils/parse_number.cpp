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
	// Trailing garbage after a valid prefix ("12abc") reports no error code.
	if (ec == std::errc() && ptr != s.data() + s.size())
		ec = std::errc::invalid_argument;
	if (ec != std::errc()) FAILED(ec);
	return value;
}
} // namespace

std::expected<int64_t, std::errc> parse_integer(std::string_view s) {
	return parse_whole<int64_t>(s);
}

std::expected<double, std::errc> parse_double(std::string_view s) {
	return parse_whole<double>(s);
}

} // namespace redis
