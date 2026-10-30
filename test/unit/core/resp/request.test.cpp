#include "core/resp/request.hpp"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <string>
#include <vector>

using namespace redis::resp;

namespace {
// One parse_request() call, with the arguments copied out so they outlive the
// views.
struct Parsed {
	std::expected<RequestKind, Parser::RespError> result;
	std::vector<std::string> args;
};

Parsed parse(std::string_view data, size_t &pos) {
	std::vector<std::string_view> views;
	std::deque<std::string> owned;
	auto result = parse_request(data, pos, views, owned);
	return {result, {views.begin(), views.end()}};
}

using Args = std::vector<std::string>;
} // namespace

SCENARIO("Parsing inline commands", "[resp][request]") {
	GIVEN("two inline commands with mixed separators and line endings") {
		const std::string_view data = "SET  key\tvalue\r\nPING\n";
		size_t pos                  = 0;

		WHEN("both are parsed in turn") {
			const auto first  = parse(data, pos);
			const auto second = parse(data, pos);

			THEN("each is split into its arguments") {
				REQUIRE(first.result == RequestKind::Command);
				REQUIRE(second.result == RequestKind::Command);
				CHECK(first.args == Args{"SET", "key", "value"});
				CHECK(second.args == Args{"PING"});
			}
			THEN("all input is consumed") { CHECK(pos == data.size()); }
		}
	}

	GIVEN("a blank line") {
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto blank = parse("\r\n", pos);

			THEN("it is consumed and yields no request") {
				REQUIRE(blank.result == RequestKind::Blank);
				CHECK(blank.args.empty());
				CHECK(pos == 2);
			}
		}
	}

	GIVEN("an inline command without its newline yet") {
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto partial = parse("PIN", pos);

			THEN("the parser asks for more bytes and consumes nothing") {
				REQUIRE_FALSE(partial.result.has_value());
				CHECK(Parser::is_incomplete(partial.result.error()));
				CHECK(pos == 0);
			}
		}
	}

	GIVEN("an inline command longer than the limit") {
		const std::string huge(MAX_INLINE_LENGTH + 1, 'a');
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto result = parse(huge, pos).result;

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
			const auto request = parse(data, pos);

			THEN("the arguments come back in order and all input is consumed") {
				REQUIRE(request.result == RequestKind::Command);
				CHECK(request.args == Args{"GET", "k"});
				CHECK(pos == data.size());
			}
		}
	}
}

SCENARIO("Request arguments are not copied", "[resp][request]") {
	std::vector<std::string_view> args;
	std::deque<std::string> owned;
	size_t pos = 0;

	const auto inside = [](std::string_view view, std::string_view data) {
		return view.data() >= data.data() &&
			   view.data() + view.size() <= data.data() + data.size();
	};

	GIVEN("a RESP array and an inline command") {
		const std::string data = "*2\r\n$3\r\nGET\r\n$1\r\nk\r\nPING\r\n";

		WHEN("both are parsed") {
			REQUIRE(parse_request(data, pos, args, owned) ==
					RequestKind::Command);
			REQUIRE(parse_request(data, pos, args, owned) ==
					RequestKind::Command);

			THEN("every argument views the input buffer") {
				REQUIRE(args.size() == 3);
				for (const auto arg : args) CHECK(inside(arg, data));
				CHECK(owned.empty());
			}
		}
	}

	GIVEN("an array of simple strings, which only the generic parser reads") {
		const std::string data = "*2\r\n+GET\r\n+k\r\n";

		WHEN("it is parsed") {
			REQUIRE(parse_request(data, pos, args, owned) ==
					RequestKind::Command);

			THEN("the decoded arguments live in the owned store") {
				CHECK(args == std::vector<std::string_view>{"GET", "k"});
				CHECK(owned.size() == 2);
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("arguments already collected and a request cut short") {
		args.emplace_back("earlier");
		const std::string data = "*2\r\n$3\r\nGET\r\n$1";

		WHEN("it is parsed") {
			const auto result = parse_request(data, pos, args, owned);

			THEN("nothing is appended and nothing consumed") {
				REQUIRE_FALSE(result.has_value());
				CHECK(Parser::is_incomplete(result.error()));
				CHECK(args == std::vector<std::string_view>{"earlier"});
				CHECK(pos == 0);
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
			const auto first  = parse(data, pos);
			const auto second = parse(data, pos);

			THEN("each comes back whole and all input is consumed") {
				REQUIRE(first.result == RequestKind::Command);
				REQUIRE(second.result == RequestKind::Command);
				CHECK(first.args == Args{"SET", "k", ""});
				CHECK(second.args == Args{"PING"});
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("a request cut at every possible byte") {
		const std::string full = "*2\r\n$3\r\nGET\r\n$5\r\nhello\r\n";

		THEN("each prefix asks for more bytes and consumes nothing") {
			for (size_t n = 1; n < full.size(); ++n) {
				size_t pos         = 0;
				const auto partial = parse(full.substr(0, n), pos);
				INFO("prefix length " << n);
				REQUIRE_FALSE(partial.result.has_value());
				CHECK(Parser::is_incomplete(partial.result.error()));
				CHECK(partial.args.empty());
				CHECK(pos == 0);
			}
		}
	}

	GIVEN("an array holding a non-bulk element") {
		const std::string_view data = "*2\r\n$3\r\nGET\r\n:1\r\n";
		size_t pos                  = 0;

		WHEN("it is parsed") {
			const auto request = parse(data, pos);

			THEN("it is consumed as a malformed request") {
				REQUIRE(request.result == RequestKind::Malformed);
				CHECK(request.args.empty());
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("a bulk length followed by garbage") {
		size_t pos = 0;

		WHEN("it is parsed") {
			const auto result = parse("*1\r\n$3x\r\nGET\r\n", pos).result;

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
			const auto request = parse(data, pos);

			THEN("the payload is cut by its length, not by line breaks") {
				REQUIRE(request.result == RequestKind::Command);
				CHECK(request.args == Args{"a\r\n\0b"s});
				CHECK(pos == data.size());
			}
		}
	}

	GIVEN("an empty and a null array") {
		THEN("both are consumed as malformed requests") {
			size_t pos = 0;
			CHECK(parse("*0\r\n", pos).result == RequestKind::Malformed);
			CHECK(pos == 4);

			pos = 0;
			CHECK(parse("*-1\r\n", pos).result == RequestKind::Malformed);
			CHECK(pos == 5);
		}
	}

	GIVEN("an array holding a null bulk string") {
		const std::string_view data = "*2\r\n$3\r\nGET\r\n$-1\r\n";
		size_t pos                  = 0;

		WHEN("it is parsed") {
			const auto request = parse(data, pos);

			THEN("it is consumed as a malformed request") {
				REQUIRE(request.result == RequestKind::Malformed);
				CHECK(request.args.empty());
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
			const auto request = parse(data, pos);

			THEN("every element arrives") {
				REQUIRE(request.result == RequestKind::Command);
				CHECK(request.args.size() == count);
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
				const auto result = parse(data, pos).result;
				INFO("input " << data);
				REQUIRE_FALSE(result.has_value());
				CHECK(result.error() == expected);
				CHECK_FALSE(Parser::is_incomplete(result.error()));
			}
		}
	}
}
