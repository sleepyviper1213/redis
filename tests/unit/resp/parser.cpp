#include "resp.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/ranges.h>

#include <string_view>
using Catch::Matchers::Equals;
using Catch::Matchers::WithinAbs;
using namespace redis::resp;

SCENARIO("[UT-2001] Encode RESP values into wire format", "[Encode][RESP]") {
	GIVEN("various RESP values") {
		WHEN("encoding a Simple String") {
			const auto val     = Value::from_simple_string("OK");
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches +OK\\r\\n") {
				REQUIRE_THAT(encoded, Equals("+OK\r\n"));
			}
		}

		WHEN("encoding a Bulk String") {
			const auto val     = Value::from_bulk_string("Hello");
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches $5\\r\\nHello\\r\\n") {
				REQUIRE_THAT(encoded, Equals("$5\r\nHello\r\n"));
			}
		}

		WHEN("encoding an Array") {
			std::vector<std::string> placeholder = {"Set", "a", "1"};
			const auto val     = Value::from_raw_array(placeholder);
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches expected RESP2 format") {
				REQUIRE_THAT(
					encoded,
					Equals("*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n"));
			}
		}

		WHEN("encoding a Simple Error") {
			const auto val = Value::from_simple_error("ERR unknown command");
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches -ERR ...") {
				REQUIRE_THAT(encoded, Equals("-ERR unknown command\r\n"));
			}
		}

		WHEN("encoding an Integer") {
			const auto val     = Value::from_integer(42);
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches :42\\r\\n") {
				REQUIRE_THAT(encoded, Equals(":42\r\n"));
			}
		}

		WHEN("encoding a Null") {
			const auto val     = Value::from_null();
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches _\\r\\n") {
				REQUIRE_THAT(encoded, Equals("_\r\n"));
			}
		}

		WHEN("encoding a Double") {
			const auto val     = Value::from_double(3.141592);
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches ,3.141592\\r\\n") {
				REQUIRE_THAT(encoded, Equals(",3.141592\r\n"));
			}
		}
	}
}

SCENARIO("[UT-2002] Decode RESP wire format into values", "[Decode][RESP]") {
	GIVEN("various RESP payloads") {
		WHEN("decoding a Simple String") {
			const std::string_view input = "+OK\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::SimpleString with content 'OK'") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_simple_string());
				REQUIRE_THAT(decoded->as_string(), Equals("OK"));
			}
		}

		WHEN("decoding a Bulk String") {
			const std::string_view input = "$5\r\nHello\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::BulkString with content 'Hello'") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_bulk_string());
				REQUIRE_THAT(decoded->as_string(), Equals("Hello"));
			}
		}

		WHEN("decoding a Double") {
			const std::string_view input = ",3.141592\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::Double ≈ 3.141592") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_double());
				REQUIRE_THAT(decoded->as_double(), WithinAbs(3.141592, 0.1));
			}
		}

		WHEN("decoding a nested array") {
			const char *input =
				"*2\r\n*3\r\n$5\r\nHello\r\n:2\r\n*0\r\n+OK\r\n";
			const auto result = Parser::parse(input);

			THEN("nested array structure is preserved") {
				REQUIRE(result.has_value());
				const Value &root = result.value();

				REQUIRE(root.is_array());
				const auto &outer = root.as_array();
				REQUIRE(outer.size() == 2);

				REQUIRE(outer[0].is_array());
				const auto &inner = outer[0].as_array();
				REQUIRE(inner.size() == 3);
				REQUIRE_THAT(inner[0].as_string(), Equals("Hello"));
				REQUIRE(inner[1].as_integer() == 2);
				REQUIRE(inner[2].as_array().empty());

				REQUIRE(outer[1].is_simple_string());
				REQUIRE_THAT(outer[1].as_string(), Equals("OK"));
			}
		}
	}
}

TEST_CASE("RESP parser benchmarks", "[benchmark][resp]") {
	// A realistic RESP input: pipelined 1000 simple SET commands
	std::string big_buffer;
	for (int i = 0; i < 1000; ++i)
		big_buffer += "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";


	BENCHMARK("Parser")
	{ return Parser::parse(big_buffer); };
}
