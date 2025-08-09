#include "resp/resp_parser.hpp"
#include "resp/value.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/ranges.h>

#include <string_view>
using resp::Parser;
using resp::Value;

TEST_CASE("Encode Simple string", "[RESP][Encode]") {
	const auto val     = Value::from_simple_string("OK");
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "+OK\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Bulk string", "[RESP][Encode]") {
	const auto val     = Value::from_bulk_string("Hello");
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "$5\r\nHello\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Array", "[RESP][Encode]") {
	std::vector<std::string> placeholder = {"Set", "a", "1"};
	const auto val                       = Value::from_raw_array(placeholder);
	const auto encoded                   = fmt::format("{:e}", val);

	const std::string_view expected =
		"*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Simple error", "[RESP][Encode]") {
	const auto val     = Value::from_simple_error("ERR unknown command");
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "-ERR unknown command\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Integer", "[RESP][Encode]") {
	const auto val     = Value::from_integer(42);
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = ":42\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Encode Null", "[RESP][Encode]") {
	const auto val     = Value::from_null();
	const auto encoded = fmt::format("{:e}", val);

	const std::string_view expected = "_\r\n";
	REQUIRE(encoded == expected);
}

TEST_CASE("Decode Simple string", "[RESP][Decode]") {
	Parser parser;
	const std::string_view input = "+OK\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->type() == Value::Type::SimpleString);
	REQUIRE(decoded->getString() == "OK");
}

TEST_CASE("Decode Bulk string", "[RESP][Decode]") {
	Parser parser;
	const std::string_view input = "$5\r\nHello\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->type() == Value::Type::BulkString);
	REQUIRE(decoded->getString() == "Hello");
}

TEST_CASE("Decode Array", "[RESP][Decode]") {
	Parser parser;
	const std::string_view input = "*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->type() == Value::Type::Array);

	const std::string_view expected_str = R"(["Set", "a", "1"])";
	REQUIRE(fmt::format("{}", decoded->getArray()) == expected_str);
}

TEST_CASE("Decode Simple error", "[RESP][Decode]") {
	Parser parser;
	const std::string_view input = "-ERR unknown command\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->type() == Value::Type::SimpleError);
	REQUIRE(decoded->getString() == "ERR unknown command");
}

TEST_CASE("Decode Integer", "[RESP][Decode]") {
	Parser parser;
	const std::string_view input = ":42\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->type() == Value::Type::Integer);
	REQUIRE(decoded->getInteger() == 42);
}

TEST_CASE("Decode Null", "[RESP][Decode]") {
	Parser parser;
	const std::string_view input = "_\r\n";

	auto decoded = parser.parse(input);
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->is_null());
}
