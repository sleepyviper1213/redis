#include "core/resp.hpp"

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
				REQUIRE(encoded == "+OK\r\n");
			}
		}

		WHEN("encoding a Bulk String") {
			const auto val     = Value::from_bulk_string("Hello");
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches $5\\r\\nHello\\r\\n") {
				REQUIRE(encoded == "$5\r\nHello\r\n");
			}
		}

		WHEN("encoding an Array") {
			std::vector<std::string> placeholder = {"Set", "a", "1"};
			const auto val     = Value::from_raw_array(placeholder);
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches expected RESP2 format") {
				REQUIRE(encoded == "*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n");
			}
		}

		WHEN("encoding a Simple Error") {
			const auto val = Value::from_simple_error("ERR unknown command");
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches -ERR ...") {
				REQUIRE(encoded == "-ERR unknown command\r\n");
			}
		}

		WHEN("encoding an Integer") {
			const auto val     = Value::from_integer(42);
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches :42\\r\\n") {
				REQUIRE(encoded == ":42\r\n");
			}
		}

		WHEN("encoding a Null") {
			const auto val     = Value::from_null();
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches _\\r\\n") { REQUIRE(encoded == "_\r\n"); }
		}

		WHEN("encoding a Double") {
			const auto val     = Value::from_double(3.141592);
			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches ,3.141592\\r\\n") {
				REQUIRE(encoded == ",3.141592\r\n");
			}
		}

		WHEN("encoding a nested Array") {
			const auto val = Value::from_array(
				{Value::from_bulk_string("SET"),
				 Value::from_bulk_string("anotherkey"),
				 Value::from_bulk_string("will expire in a minute"),
				 Value::from_bulk_string("EX"),
				 Value::from_bulk_string("60")});

			const char *expected_str = "*5\r\n"
									   "$3\r\nSET\r\n"
									   "$10\r\nanotherkey\r\n"
									   "$23\r\nwill expire in a minute\r\n"
									   "$2\r\nEX\r\n"
									   "$2\r\n60\r\n";

			const auto encoded = fmt::format("{:e}", val);

			THEN("the result matches the full nested RESP encoding") {
				REQUIRE(encoded == expected_str);
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
				REQUIRE(decoded->as_string() == "OK");
			}
		}

		WHEN("decoding a Bulk String") {
			const std::string_view input = "$5\r\nHello\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::BulkString with content 'Hello'") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_bulk_string());
				REQUIRE(decoded->as_string() == "Hello");
			}
		}

		WHEN("decoding an Array") {
			const std::string_view input =
				"*3\r\n$3\r\nSet\r\n$1\r\na\r\n$1\r\n1\r\n";
			const auto decoded = Parser::parse(input);

			THEN("it becomes a Value::Array with correct elements") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_array());
				REQUIRE(fmt::format("{:?}", *decoded) ==
						R"((array) "Set" "a" "1")");
			}
		}

		WHEN("decoding a Simple Error") {
			const std::string_view input = "-ERR unknown command\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::Error with matching message") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_error());
				REQUIRE(decoded->as_string() == "ERR unknown command");
			}
		}

		WHEN("decoding an Integer") {
			const std::string_view input = ":42\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::Integer 42") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_integer());
				REQUIRE(decoded->as_integer() == 42);
			}
		}

		WHEN("decoding a Null") {
			const std::string_view input = "_\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::Null") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_null());
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

		WHEN("decoding an Empty Simple String") {
			const std::string_view input = "+\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes an empty SimpleString") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_simple_string());
				REQUIRE(decoded->as_string().empty());
			}
		}

		WHEN("decoding an Empty Bulk String") {
			const std::string_view input = "$0\r\n\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes an empty BulkString") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_bulk_string());
				REQUIRE(decoded->as_string().empty());
			}
		}

		WHEN("decoding a Negative Integer") {
			const std::string_view input = ":-42\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes a Value::Integer -42") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_integer());
				REQUIRE(decoded->as_integer() == -42);
			}
		}

		WHEN("decoding a Large Integer") {
			const std::string_view input = ":9223372036854775807\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes LLONG_MAX") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_integer());
				REQUIRE(decoded->as_integer() == 9'223'372'036'854'775'807LL);
			}
		}

		WHEN("decoding special Double values") {
			SECTION("Infinity") {
				const std::string_view input = ",inf\r\n";
				const auto decoded           = Parser::parse(input);

				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_double());
				REQUIRE(std::isinf(decoded->as_double()));
			}
			SECTION("-Infinity") {
				const std::string_view input = ",-inf\r\n";
				const auto decoded           = Parser::parse(input);

				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_double());
				REQUIRE(std::isinf(decoded->as_double()));
				REQUIRE(decoded->as_double() < 0);
			}
			SECTION("NaN") {
				const std::string_view input = ",nan\r\n";
				const auto decoded           = Parser::parse(input);

				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_double());
				REQUIRE(std::isnan(decoded->as_double()));
			}
		}

		WHEN("decoding Null Bulk String") {
			const std::string_view input = "$-1\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes Null") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_null());
			}
		}

		WHEN("decoding Null Array") {
			const std::string_view input = "*-1\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes Null") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_null());
			}
		}

		WHEN("decoding Empty Array") {
			const std::string_view input = "*0\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it becomes an empty Array") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_array());
				REQUIRE(decoded->as_array().empty());
			}
		}

		WHEN("decoding an Array with mixed types") {
			const std::string_view input = "*3\r\n+OK\r\n:42\r\n_\r\n";
			const auto decoded           = Parser::parse(input);

			THEN("it contains string, integer, and null") {
				REQUIRE(decoded.has_value());
				REQUIRE(decoded->is_array());

				const auto arr = decoded->as_array();
				REQUIRE(arr.size() == 3);
				REQUIRE(arr[0].as_string() == "OK");
				REQUIRE(arr[1].as_integer() == 42);
				REQUIRE(arr[2].is_null());
			}
		}

		WHEN("decoding malformed string (missing CRLF)") {
			const std::string_view input = "+OK";
			const auto decoded           = Parser::parse(input);

			THEN("parsing fails") { REQUIRE_FALSE(decoded.has_value()); }
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
				REQUIRE(outer[1].as_string() == "OK");
			}
		}
	}
}

SCENARIO("Parsing RESP data with invalid prefix", "[resp][parser]") {
	GIVEN("A data string starting with an unknown prefix '!'") {
		std::string_view data = "!\r\n";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return NOT_SUPPORTED error") {
				REQUIRE(!result);
				REQUIRE(result.error() == Parser::RespError::NOT_SUPPORTED);
			}
		}
	}
}

SCENARIO("Parsing RESP array with invalid negative length less than -1",
		 "[resp][parser]") {
	GIVEN("An array prefix with length -2") {
		std::string_view data = "*-2\r\n";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return INVALID_ARRAY_LENGTH error") {
				REQUIRE(!result);
				REQUIRE(result.error() ==
						Parser::RespError::INVALID_ARRAY_LENGTH);
			}
		}
	}
}

SCENARIO("Parsing RESP bulk string with truncated data", "[resp][parser]") {
	GIVEN("A bulk string declaring length 5 but only 3 characters provided") {
		std::string_view data = "$5\r\nabc";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return TRUNCATED_BULK_STRING error") {
				REQUIRE(!result);
				REQUIRE(result.error() ==
						Parser::RespError::TRUNCATED_BULK_STRING);
			}
		}
	}
}

SCENARIO("Parsing RESP bulk string missing CRLF after data", "[resp][parser]") {
	GIVEN("A bulk string with data but no terminating CRLF") {
		std::string_view data = "$3\r\nabc";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return TRUNCATED_BULK_STRING or MISSING_CRLF "
				 "error") {
				REQUIRE(!result);
				REQUIRE((result.error() ==
							 Parser::RespError::TRUNCATED_BULK_STRING ||
						 result.error() == Parser::RespError::MISSING_CRLF));
			}
		}
	}
}

SCENARIO("Parsing RESP array with missing elements leading to unexpected EOF",
		 "[resp][parser]") {
	GIVEN("An array of length 2 with only one element provided") {
		std::string_view data = "*2\r\n+OK\r\n";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return UNEXPECTED_EOF error") {
				REQUIRE(!result);
				REQUIRE(result.error() == Parser::RespError::UNEXPECTED_EOF);
			}
		}
	}
}

SCENARIO("Parsing RESP double with invalid payload", "[resp][parser]") {
	GIVEN("A double prefix with non-numeric payload 'abc'") {
		std::string_view data = ",abc\r\n";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return PARSE_NUMBER error") {
				REQUIRE(!result);
				REQUIRE(result.error() == Parser::RespError::PARSE_NUMBER);
			}
		}
	}
}

SCENARIO("Parsing RESP null with unexpected payload", "[resp][parser]") {
	GIVEN("A null prefix '_' followed by non-empty payload") {
		std::string_view data = "_abc\r\n";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return SYNTAX_ERROR") {
				REQUIRE(!result);
				REQUIRE(result.error() == Parser::RespError::SYNTAX_ERROR);
			}
		}
	}
}

SCENARIO("Parsing RESP data with unexpected empty line in invalid position",
		 "[resp][parser]") {
	GIVEN("An array with an extra empty line between elements") {
		std::string_view data = "*2\r\n+OK\r\n\r\n+OK\r\n";

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return EMPTY_LINE error") {
				REQUIRE(!result);
				REQUIRE(result.error() == Parser::RespError::EMPTY_LINE);
			}
		}
	}
}

SCENARIO("Parsing complex nested RESP array with mixed types and nulls",
		 "[resp][parser]") {
	GIVEN("A nested array including bulk string, integer, empty array, null, "
		  "and double") {
		std::string_view data =
			"*5\r\n$5\r\nHello\r\n:42\r\n*0\r\n_-1\r\n,3.14\r\n"; // Note: _-1
																  // is invalid
																  // syntax for
																  // null

		WHEN("The parser is called") {
			auto result = Parser::parse(data);

			THEN("It should return SYNTAX_ERROR due to invalid null syntax") {
				REQUIRE(!result);
				REQUIRE(result.error() == Parser::RespError::SYNTAX_ERROR);
			}
		}
	}
}
