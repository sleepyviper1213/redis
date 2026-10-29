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

SCENARIO("Parsing RESP arrays outside the bulk-string fast path",
		 "[resp][request]") {
	GIVEN("two pipelined requests, one with an empty bulk string") {
		const std::string_view data =
			"*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$0\r\n\r\n*1\r\n$4\r\nPING\r\n";
		size_t pos = 0;

		WHEN("both are parsed in turn") {
			const auto first  = parse_request(data, pos);
			const auto second = parse_request(data, pos);

			THEN("each comes back whole and all input is consumed") {
				REQUIRE(first.has_value());
				REQUIRE(second.has_value());
				CHECK(strings(**first) ==
					  std::vector<std::string>{"SET", "k", ""});
				CHECK(strings(**second) == std::vector<std::string>{"PING"});
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("a request cut at every possible byte") {
		const std::string full = "*2\r\n$3\r\nGET\r\n$5\r\nhello\r\n";

		THEN("each prefix asks for more bytes and consumes nothing") {
			for (size_t n = 1; n < full.size(); ++n) {
				size_t pos        = 0;
				const auto result = parse_request(full.substr(0, n), pos);
				INFO("prefix length " << n);
				REQUIRE_FALSE(result.has_value());
				CHECK(Parser::is_incomplete(result.error()));
				CHECK(pos == 0);
			}
		}
	}

	GIVEN("an array holding a non-bulk element") {
		const std::string_view data = "*2\r\n$3\r\nGET\r\n:1\r\n";
		size_t pos                  = 0;

		WHEN("it is parsed") {
			const auto result = parse_request(data, pos);

			THEN("the generic parser still accepts it") {
				REQUIRE(result.has_value());
				CHECK((**result).as_array().size() == 2);
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("a bulk length followed by garbage") {
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto result = parse_request("*1\r\n$3x\r\nGET\r\n", pos);

			THEN("it is a protocol error, not a request for more bytes") {
				REQUIRE_FALSE(result.has_value());
				CHECK_FALSE(Parser::is_incomplete(result.error()));
			}
		}
	}
}

SCENARIO("Parsing request lengths and payloads", "[resp][request]") {
	GIVEN("a bulk payload that contains CRLF and a NUL byte") {
		using namespace std::string_literals;
		const std::string data = "*1\r\n$5\r\na\r\n\0b\r\n"s;
		size_t pos             = 0;

		WHEN("it is parsed") {
			const auto result = parse_request(data, pos);

			THEN("the payload is cut by its length, not by line breaks") {
				REQUIRE(result.has_value());
				CHECK(strings(**result) ==
					  std::vector<std::string>{"a\r\n\0b"s});
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("an empty and a null array") {
		THEN("both still parse through the generic parser") {
			size_t pos       = 0;
			const auto empty = parse_request("*0\r\n", pos);
			REQUIRE(empty.has_value());
			CHECK((**empty).as_array().empty());
			CHECK(pos == 4);

			pos             = 0;
			const auto null = parse_request("*-1\r\n", pos);
			REQUIRE(null.has_value());
			CHECK((**null).is_null());
			CHECK(pos == 5);
		}
	}

	GIVEN("an array holding a null bulk string") {
		const std::string_view data = "*2\r\n$3\r\nGET\r\n$-1\r\n";
		size_t pos                  = 0;

		WHEN("it is parsed") {
			const auto result = parse_request(data, pos);

			THEN("the null element is kept") {
				REQUIRE(result.has_value());
				const auto &args = (**result).as_array();
				REQUIRE(args.size() == 2);
				CHECK(args[0].as_string() == "GET");
				CHECK(args[1].is_null());
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("more elements than the fast path reserves") {
		const size_t count = 1100;
		std::string data   = "*" + std::to_string(count) + "\r\n";
		for (size_t i = 0; i < count; ++i) data += "$1\r\na\r\n";
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto result = parse_request(data, pos);

			THEN("every element arrives") {
				REQUIRE(result.has_value());
				CHECK((**result).as_array().size() == count);
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("malformed lengths and payloads") {
		THEN("each is a protocol error, not a request for more bytes") {
			const std::pair<std::string_view, Parser::RespError> cases[] = {
				{"*1\r\n$99999999999999999999\r\n",
				 Parser::RespError::PARSE_NUMBER},
				{"*1\r\n$999999999999\r\n", Parser::RespError::INVALID_BULK_LENGTH},
				{"*1\r\n$3\r\nGETXY", Parser::RespError::MISSING_CRLF},
				{"*x\r\n", Parser::RespError::PARSE_NUMBER},
			};
			for (const auto &[data, expected] : cases) {
				size_t pos        = 0;
				const auto result = parse_request(data, pos);
				INFO("input " << data);
				REQUIRE_FALSE(result.has_value());
				CHECK(result.error() == expected);
				CHECK_FALSE(Parser::is_incomplete(result.error()));
			}
		}
	}
}
