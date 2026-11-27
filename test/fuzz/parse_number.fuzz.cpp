// Fuzzes parse_integer and parse_double, which read every numeric argument
// (INCRBY, EXPIRE, LRANGE, SPOP counts...) and the lengths RESP declares
// (TC-330).
//
// - parse_integer agrees with a reference: Redis's canonical spelling of an
//   int64 (string2ll: no '+', no spaces, no leading zero, no "-0"), with
//   std::from_chars for the range. What it accepts prints back to the input.
// - parse_double accepts what parse_integer does, as the nearest double.
// - A double parse_double accepts prints, in {fmt}'s shortest form, to a string
//   it parses back to the same bits (to a NaN, from a NaN).

#include "core/error/verify.hpp"
#include "core/utils/parse_number.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

/// `0`, or `-?[1-9][0-9]*`: the only spellings string2ll accepts.
bool is_canonical_integer(std::string_view s) {
	const bool is_negative        = s.starts_with('-');
	const std::string_view digits = is_negative ? s.substr(1) : s;
	if (digits.empty() || !std::ranges::all_of(digits, is_digit)) return false;
	if (digits.front() != '0') return true;
	return digits.size() == 1 && !is_negative;
}

std::expected<int64_t, std::errc> reference_integer(std::string_view s) {
	if (!is_canonical_integer(s))
		return std::unexpected(std::errc::invalid_argument);
	int64_t value{};
	const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
	if (ec != std::errc()) return std::unexpected(ec);
	REDIS_VERIFY(end == s.data() + s.size(),
				 "from_chars stopped inside a canonical integer");
	return value;
}

void check_integer(std::string_view input) {
	const auto parsed = redis::parse_integer(input);
	REDIS_VERIFY(parsed == reference_integer(input),
				 "parse_integer disagrees with the canonical spelling");
	if (!parsed) return;

	REDIS_VERIFY(fmt::format("{}", *parsed) == input,
				 "an accepted integer does not print back to its input");
	const auto as_double = redis::parse_double(input);
	REDIS_VERIFY(as_double && *as_double == static_cast<double>(*parsed),
				 "parse_double rejects or misreads an integer");
}

void check_double_round_trip(std::string_view input) {
	const auto parsed = redis::parse_double(input);
	if (!parsed) return;

	const std::string printed = fmt::format("{}", *parsed);
	const auto reparsed       = redis::parse_double(printed);
	REDIS_VERIFY(reparsed.has_value(),
				 "parse_double rejects how {fmt} prints a double it accepted");
	if (std::isnan(*parsed)) {
		REDIS_VERIFY(std::isnan(*reparsed), "a NaN reads back as a number");
		return;
	}
	REDIS_VERIFY(std::bit_cast<uint64_t>(*reparsed) ==
					 std::bit_cast<uint64_t>(*parsed),
				 "a double changed going through print and parse");
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	const std::string_view input(reinterpret_cast<const char *>(data), size);
	check_integer(input);
	check_double_round_trip(input);
	return 0;
}
