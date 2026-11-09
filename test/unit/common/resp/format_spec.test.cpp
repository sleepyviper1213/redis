#include "common/resp/value.hpp"
#include "core/utils/format_spec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <array>
#include <string_view>

using namespace redis;

namespace {
// What a formatter's parse() sees: the specification and the rest of the format
// string, up to the closing brace.
constexpr char presentation_of(std::string_view spec, std::string_view allowed) {
	fmt::format_parse_context ctx(spec);
	return parse_presentation(ctx, allowed);
}
} // namespace

// Failures are reported by fmt::report_error, which a constant expression
// cannot reach: so what is accepted can be checked at compile time.
static_assert(presentation_of("}", "?e") == 0);
static_assert(presentation_of("e}", "?e") == 'e');
static_assert(presentation_of("?}", "?e") == '?');

SCENARIO("A presentation character is read from a format specification",
		 "[format_spec]") {
	GIVEN("a formatter that allows ? and e") {
		THEN("an empty specification is no presentation") {
			CHECK(presentation_of("}", "?e") == 0);
		}
		THEN("an allowed character is returned") {
			CHECK(presentation_of("e}", "?e") == 'e');
			CHECK(presentation_of("?}", "?e") == '?');
		}
		THEN("a character that is not allowed is refused") {
			CHECK_THROWS_AS(presentation_of("x}", "?e"), fmt::format_error);
			CHECK_THROWS_AS(presentation_of("e}", "?"), fmt::format_error);
		}
		THEN("anything after the character is refused") {
			CHECK_THROWS_AS(presentation_of("ee}", "?e"), fmt::format_error);
			CHECK_THROWS_AS(presentation_of("e>5}", "?e"), fmt::format_error);
		}
	}

	GIVEN("a specification that stops before its closing brace") {
		// No terminator after the last character: reading one more would be a
		// read past the end, which is what the check guards against.
		constexpr std::array<char, 1> unfinished{'e'};
		fmt::format_parse_context ctx(
			std::string_view(unfinished.data(), unfinished.size()));

		THEN("the character is read and the end is left alone") {
			CHECK(parse_presentation(ctx, "?e") == 'e');
			CHECK(ctx.begin() == ctx.end());
		}
	}
}

SCENARIO("The formatters built on it accept exactly what they document",
		 "[format_spec]") {
	GIVEN("a RESP value") {
		const auto value = resp::Value::from_integer(5);

		THEN("{}, {:e} and {:?} are accepted") {
			CHECK(fmt::format("{}", value) == ":5\r\n");
			CHECK(fmt::format("{:e}", value) == ":5\r\n");
			CHECK_FALSE(fmt::format("{:?}", value).empty());
		}
		THEN("anything else is refused, and an unfinished specification too") {
			CHECK_THROWS_AS(fmt::format(fmt::runtime("{:x}"), value), fmt::format_error);
			CHECK_THROWS_AS(fmt::format(fmt::runtime("{:e5}"), value), fmt::format_error);
			CHECK_THROWS_AS(fmt::format(fmt::runtime("{:e"), value), fmt::format_error);
			CHECK_THROWS_AS(fmt::format(fmt::runtime("{:?"), value), fmt::format_error);
		}
	}
}
