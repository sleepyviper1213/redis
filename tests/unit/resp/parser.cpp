#include "resp.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fmt/ranges.h>

#include <string_view>
using Catch::Matchers::WithinAbs;
using redis::resp::Parser;
using redis::resp::Value;

TEST_CASE("Encode Simple string", "[Encode][RESP2][RESP3][SimpleString]") {
	const auto val     = Value::from_simple_string("OK");
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "+OK\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Bulk string", "[Encode][RESP2][BulkString]") {
	const auto val     = Value::from_bulk_string("Hello");
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "$5\r\nHello\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Array", "[Encode][RESP2][Array]") {
	std::vector<std::string> placeholder = {"Set", "a", "1"};
	const auto val                       = Value::from_raw_array(placeholder);
	const auto encoded                   = fmt::format("{:e}", val);

	const std::string_view expected =
		"*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Simple error", "[Encode][RESP2][SimpleError]") {
	const auto val     = Value::from_simple_error("ERR unknown command");
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "-ERR unknown command\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Integer", "[Encode][RESP2][RESP3][Integer]") {
	const auto val     = Value::from_integer(42);
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = ":42\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Null", "[Encode][RESP3][Null]") {
	const auto val     = Value::from_null();
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "_\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode RESP3 Double", "[Encode][RESP3][Double]") {
	const auto val     = Value::from_double(3.141592);
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = ",3.141592\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode nested value", "[Encode][nested]") {
	const auto val =
		Value::from_array({Value::from_bulk_string("SET"),
						   Value::from_bulk_string("anotherkey"),
						   Value::from_bulk_string("will expire in a minute"),
						   Value::from_bulk_string("EX"),
						   Value::from_bulk_string("60")});

	// clang-format off
	const auto *expected_str = "*5\r\n"
							   "$3\r\nSET\r\n"
							   "$10\r\nanotherkey\r\n"
							   "$23\r\nwill expire in a minute\r\n"
							   "$2\r\nEX\r\n"
							   "$2\r\n60\r\n";

	// clang-format on
	const auto encoded = fmt::format("{:e}", val);

	REQUIRE(encoded == expected_str);
}

TEST_CASE("Decode Simple string", "[Decode][RESP2][RESP3][SimpleString]") {
	const std::string_view input = "+OK\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_simple_string());
	REQUIRE(decoded->as_string() == "OK");
}

TEST_CASE("Decode Bulk string", "[Decode][RESP2][BulkString]") {
	const std::string_view input = "$5\r\nHello\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_bulk_string());
	REQUIRE(decoded->as_string() == "Hello");
}

TEST_CASE("Decode Array", "[Decode][RESP2][Array]") {
	const std::string_view input = "*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_array());

	const auto expected_str = R"((array) "Set" "a" "1")";
	const auto formatted    = fmt::format("{:?}", *decoded);
	REQUIRE(formatted == expected_str);
}

TEST_CASE("Decode Simple error", "[Decode][RESP2][SimpleError]") {
	const std::string_view input = "-ERR unknown command\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_error());
	REQUIRE(decoded->as_string() == "ERR unknown command");
}

TEST_CASE("Decode Integer", "[Decode][RESP2][RESP3][Integer]") {
	const std::string_view input = ":42\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_integer());
	REQUIRE(decoded->as_integer() == 42);
}

TEST_CASE("Decode Null", "[Decode][RESP3][Null]") {
	const std::string_view input = "_\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_null());
}

TEST_CASE("Decode RESP3 Double", "[Decode][RESP3][Double]") {
	const std::string_view input = ",3.141592\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_double());
	REQUIRE_THAT(decoded->as_double(), WithinAbs(3.141592, 0.1));
}

TEST_CASE("Decode Empty Simple string",
		  "[Decode][RESP2][RESP3][SimpleString][Edge]") {
	const std::string_view input = "+\r\n"; // zero-length simple string

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_simple_string());
	REQUIRE(decoded->as_string().empty());
}

TEST_CASE("Decode Empty Bulk string", "[Decode][RESP2][BulkString][Edge]") {
	const std::string_view input =
		"$0\r\n\r\n"; // declared length 0, followed by CRLF

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_bulk_string());
	REQUIRE(decoded->as_string().empty());
}

TEST_CASE("Decode Negative Integer", "[Decode][RESP2][RESP3][Integer][Edge]") {
	const std::string_view input = ":-42\r\n";

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_integer());
	REQUIRE(decoded->as_integer() == -42);
}

TEST_CASE("Decode Large Integer", "[Decode][RESP2][RESP3][Integer][Edge]") {
	const std::string_view input = ":9223372036854775807\r\n"; // LLONG_MAX

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_integer());
	REQUIRE(decoded->as_integer() == 9'223'372'036'854'775'807LL);
}

TEST_CASE("Decode Special Double Values", "[Decode][RESP3][Double][Edge]") {
	SECTION("Infinity") {
		const std::string_view input = ",inf\r\n";
		auto decoded                 = Parser::parse(input);
		REQUIRE(decoded.has_value());
		REQUIRE(decoded->is_double());
		REQUIRE(std::isinf(decoded->as_double()));
	}

	SECTION("-Infinity") {
		const std::string_view input = ",-inf\r\n";
		auto decoded                 = Parser::parse(input);
		REQUIRE(decoded.has_value());
		REQUIRE(decoded->is_double());
		REQUIRE(std::isinf(decoded->as_double()));
		REQUIRE(decoded->as_double() < 0);
	}

	SECTION("NaN") {
		const std::string_view input = ",nan\r\n";
		auto decoded                 = Parser::parse(input);
		REQUIRE(decoded.has_value());
		REQUIRE(decoded->is_double());
		REQUIRE(std::isnan(decoded->as_double()));
	}
}

TEST_CASE("Decode Null Bulk String",
		  "[Decode][RESP2][BulkString][Null][Edge]") {
	const std::string_view input = "$-1\r\n"; // RESP2 null bulk string

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_null());
}

TEST_CASE("Decode Null Array", "[Decode][RESP2][Array][Null][Edge]") {
	const std::string_view input = "*-1\r\n"; // RESP2 null array

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_null());
}

TEST_CASE("Decode Empty Array", "[Decode][RESP2][Array][Edge]") {
	const std::string_view input = "*0\r\n"; // empty array

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_array());
	REQUIRE(decoded->as_array().empty());
}

TEST_CASE("Decode Array with Mixed Types",
		  "[Decode][RESP2][Array][Mixed][Edge]") {
	const std::string_view input =
		"*3\r\n+OK\r\n:42\r\n_\r\n"; // simple string, integer, null

	auto decoded = Parser::parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_array());

	auto arr = decoded->as_array();
	REQUIRE(arr.size() == 3);
	REQUIRE(arr[0].as_string() == "OK");
	REQUIRE(arr[1].as_integer() == 42);
	REQUIRE(arr[2].is_null());
}

TEST_CASE("Decode string with missing CRLF",
		  "[Decode][RESP2][RESP3][Malformed][Edge]") {
	const std::string_view input = "+OK";

	auto decoded = Parser::parse(input);
	REQUIRE_FALSE(decoded.has_value());
}

TEST_CASE("Decode nested array", "[Decode][nested]") {
	// Example: *2\r\n*3\r\n:1\r\n:2\r\n:3\r\n+OK\r\n
	// Meaning: Array of 2 elements:
	//   1. Array of 3 integers: 1, 2, 3
	//   2. Simple String "OK"
	const auto input = "*2\r\n*3\r\n:1\r\n:2\r\n:3\r\n+OK\r\n";

	const auto result = Parser::parse(input);

	REQUIRE(result.has_value());
	const Value &root = result.value();

	REQUIRE(root.is_array());
	const auto &outer = root.as_array();
	REQUIRE(outer.size() == 2);

	// First element: nested array
	REQUIRE(outer[0].is_array());
	const auto &inner = outer[0].as_array();
	REQUIRE(inner.size() == 3);
	REQUIRE(inner[0].as_integer() == 1);
	REQUIRE(inner[1].as_integer() == 2);
	REQUIRE(inner[2].as_integer() == 3);

	// Second element: simple string
	REQUIRE(outer[1].is_simple_string());
	REQUIRE(outer[1].as_string() == "OK");
}
