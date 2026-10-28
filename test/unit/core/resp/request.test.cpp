#include "core/resp/request.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace redis::resp;

namespace {
std::vector<std::string> strings(const Value &request) {
	std::vector<std::string> out;
	for (const auto &arg : request.as_array()) out.push_back(arg.as_string());
	return out;
}
} // namespace

SCENARIO("Parsing inline commands", "[resp][request]") {
	GIVEN("two inline commands with mixed separators and line endings") {
		const std::string_view data = "SET  key\tvalue\r\nPING\n";
		size_t pos                  = 0;

		WHEN("both are parsed in turn") {
			const auto first  = parse_request(data, pos);
			const auto second = parse_request(data, pos);

			THEN("each is split into its arguments") {
				REQUIRE(first.has_value());
				REQUIRE(second.has_value());
				CHECK(strings(**first) ==
					  std::vector<std::string>{"SET", "key", "value"});
				CHECK(strings(**second) == std::vector<std::string>{"PING"});
			}
			THEN("all input is consumed") { CHECK(pos == data.size()); }
		}
	}

	GIVEN("a blank line") {
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto result = parse_request("\r\n", pos);

			THEN("it is consumed and yields no request") {
				REQUIRE(result.has_value());
				CHECK_FALSE(result->has_value());
				CHECK(pos == 2);
			}
		}
	}

	GIVEN("an inline command without its newline yet") {
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto result = parse_request("PIN", pos);

			THEN("the parser asks for more bytes and consumes nothing") {
				REQUIRE_FALSE(result.has_value());
				CHECK(Parser::is_incomplete(result.error()));
				CHECK(pos == 0);
			}
		}
	}

	GIVEN("an inline command longer than the limit") {
		const std::string huge(MAX_INLINE_LENGTH + 1, 'a');
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto result = parse_request(huge, pos);

			THEN("it is rejected instead of buffered forever") {
				REQUIRE_FALSE(result.has_value());
				CHECK(result.error() == Parser::RespError::INLINE_TOO_LONG);
				CHECK_FALSE(Parser::is_incomplete(result.error()));
			}
		}
	}
}

SCENARIO("Parsing RESP array requests", "[resp][request]") {
	GIVEN("a RESP array of bulk strings") {
		const std::string_view data = "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n";
		size_t pos                  = 0;

		WHEN("it is parsed") {
			const auto result = parse_request(data, pos);

			THEN("the arguments come back in order and all input is consumed") {
				REQUIRE(result.has_value());
				CHECK(strings(**result) == std::vector<std::string>{"GET", "k"});
				CHECK(pos == data.size());
			}
		}
	}
}
