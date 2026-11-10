#include "core/utils/parse_number.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <system_error>

using namespace redis;

SCENARIO("Parsing decimal integers", "[parse][integer]") {
	THEN("valid decimals parse, including the int64 bounds") {
		CHECK(parse_integer("0") == 0);
		CHECK(parse_integer("42") == 42);
		CHECK(parse_integer("-7") == -7);
		CHECK(parse_integer("9223372036854775807") == INT64_MAX);
		CHECK(parse_integer("-9223372036854775808") == INT64_MIN);
	}

	THEN("input with no digits is invalid") {
		for (const std::string_view s : {"", "-", "abc"}) {
			INFO("input " << s);
			const auto result = parse_integer(s);
			REQUIRE(!result);
			CHECK(result.error() == std::errc::invalid_argument);
		}
	}

	THEN("trailing characters after the digits are invalid") {
		for (const std::string_view s : {"12abc", "1.5", "7 "}) {
			INFO("input " << s);
			const auto result = parse_integer(s);
			REQUIRE(!result);
			CHECK(result.error() == std::errc::invalid_argument);
		}
	}

	THEN("values outside int64 are out of range") {
		for (const std::string_view s :
			 {"9223372036854775808", "-9223372036854775809",
			  "99999999999999999999"}) {
			INFO("input " << s);
			const auto result = parse_integer(s);
			REQUIRE(!result);
			CHECK(result.error() == std::errc::result_out_of_range);
		}
	}
}
