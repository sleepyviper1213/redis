#include "resp/resp_parser.hpp"
#include "resp/value.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fmt/ranges.h>

#include <string_view>
using Catch::Matchers::WithinAbs;
using resp::Parser;
using resp::Value;

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

TEST_CASE("Decode Simple string", "[Decode][RESP2][RESP3][SimpleString]") {
	Parser parser;
	const std::string_view input = "+OK\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_simple_string());
	REQUIRE(decoded->getString() == "OK");
}

TEST_CASE("Decode Bulk string", "[Decode][RESP2][BulkString]") {
	Parser parser;
	const std::string_view input = "$5\r\nHello\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_bulk_string());
	REQUIRE(decoded->getString() == "Hello");
}

TEST_CASE("Decode Array", "[Decode][RESP2][Array]") {
	Parser parser;
	const std::string_view input = "*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_array());

	const std::string_view expected_str = R"(["Set", "a", "1"])";
	REQUIRE(fmt::format("{}", decoded->getArray()) == expected_str);
}

TEST_CASE("Decode Simple error", "[Decode][RESP2][SimpleError]") {
	Parser parser;
	const std::string_view input = "-ERR unknown command\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->type() == Value::Type::SimpleError);
	REQUIRE(decoded->getString() == "ERR unknown command");
}

TEST_CASE("Decode Integer", "[Decode][RESP2][RESP3][Integer]") {
	Parser parser;
	const std::string_view input = ":42\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_integer());
	REQUIRE(decoded->getInteger() == 42);
}

TEST_CASE("Decode Null", "[Decode][RESP3][Null]") {
	Parser parser;
	const std::string_view input = "_\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_null());
}

TEST_CASE("Decode RESP3 Double", "[Decode][RESP3][Double]") {
	Parser parser;
	const std::string_view input = ",3.141592\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_double());
	REQUIRE_THAT(decoded->getDouble(), WithinAbs(3.141592, 0.1));
}

TEST_CASE("Decode Empty Simple string",
		  "[Decode][RESP2][RESP3][SimpleString][Edge]") {
	Parser parser;
	const std::string_view input = "+\r\n"; // zero-length simple string

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_simple_string());
	REQUIRE(decoded->getString().empty());
}

TEST_CASE("Decode Empty Bulk string", "[Decode][RESP2][BulkString][Edge]") {
	Parser parser;
	const std::string_view input =
		"$0\r\n\r\n"; // declared length 0, followed by CRLF

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_bulk_string());
	REQUIRE(decoded->getString().empty());
}

TEST_CASE("Decode Negative Integer", "[Decode][RESP2][RESP3][Integer][Edge]") {
	Parser parser;
	const std::string_view input = ":-42\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_integer());
	REQUIRE(decoded->getInteger() == -42);
}

TEST_CASE("Decode Large Integer", "[Decode][RESP2][RESP3][Integer][Edge]") {
	Parser parser;
	const std::string_view input = ":9223372036854775807\r\n"; // LLONG_MAX

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_integer());
	REQUIRE(decoded->getInteger() == 9'223'372'036'854'775'807LL);
}

TEST_CASE("Decode Special Double Values", "[Decode][RESP3][Double][Edge]") {
	Parser parser;

	SECTION("Infinity") {
		const std::string_view input = ",inf\r\n";
		auto decoded                 = parser.parse(input);
		REQUIRE(decoded.has_value());
		REQUIRE(decoded->is_double());
		REQUIRE(std::isinf(decoded->getDouble()));
	}

	SECTION("-Infinity") {
		const std::string_view input = ",-inf\r\n";
		auto decoded                 = parser.parse(input);
		REQUIRE(decoded.has_value());
		REQUIRE(decoded->is_double());
		REQUIRE(std::isinf(decoded->getDouble()));
		REQUIRE(decoded->getDouble() < 0);
	}

	SECTION("NaN") {
		const std::string_view input = ",nan\r\n";
		auto decoded                 = parser.parse(input);
		REQUIRE(decoded.has_value());
		REQUIRE(decoded->is_double());
		REQUIRE(std::isnan(decoded->getDouble()));
	}
}

TEST_CASE("Decode Null Bulk String",
		  "[Decode][RESP2][BulkString][Null][Edge]") {
	Parser parser;
	const std::string_view input = "$-1\r\n"; // RESP2 null bulk string

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_bulk_string());
}

TEST_CASE("Decode Null Array", "[Decode][RESP2][Array][Null][Edge]") {
	Parser parser;
	const std::string_view input = "*-1\r\n"; // RESP2 null array

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_null());
}

TEST_CASE("Decode Empty Array", "[Decode][RESP2][Array][Edge]") {
	Parser parser;
	const std::string_view input = "*0\r\n"; // empty array

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_array());
	REQUIRE(decoded->getArray().empty());
}

TEST_CASE("Decode Array with Mixed Types",
		  "[Decode][RESP2][Array][Mixed][Edge]") {
	Parser parser;
	const std::string_view input =
		"*3\r\n+OK\r\n:42\r\n_\r\n"; // simple string, integer, null

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_array());

	auto arr = decoded->getArray();
	REQUIRE(arr.size() == 3);
	REQUIRE(arr[0].getString() == "OK");
	REQUIRE(arr[1].getInteger() == 42);
	REQUIRE(arr[2].is_null());
}

TEST_CASE("Decode string with missing CRLF",
		  "[Decode][RESP2][RESP3][Malformed][Edge]") {
	Parser parser;
	const std::string_view input = "+OK";

	auto decoded = parser.parse(input);
	REQUIRE_FALSE(decoded.has_value());
}
