#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/format.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;

using namespace redis;
using namespace std::chrono_literals;

namespace {
/// PTTL of \p key as a number, for checks that allow for time passing.
long long pttl(test::Server &server, const std::string &key) {
	const auto reply = server.run({"PTTL", key});
	REQUIRE(reply.starts_with(':'));
	return std::stoll(reply.substr(1));
}
} // namespace

SCENARIO("SET applies each expiration option",
		 "[commands][set]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("TC-093: a key is set with EX") {
			REQUIRE(server.run({"SET", "k", "v", "EX", "100"}) == "+OK\r\n");
			THEN("TC-104: its TTL is in seconds") {
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
			}
		}

		WHEN("TC-093: a key is set with PX") {
			// Long enough not to pass during the test, even under a sanitizer.
			REQUIRE(server.run({"SET", "k", "v", "PX", "100000"}) == "+OK\r\n");
			THEN("its TTL is in milliseconds") {
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
				CHECK(pttl(server, "k") > 99'000);
				CHECK(pttl(server, "k") <= 100'000);
			}
		}

		WHEN("TC-093: keys are set with EXAT and PXAT") {
			const auto in_100s =
				DateTime::current().to_secs_since_epoch() + 100;
			REQUIRE(server.run({"SET", "a", "v", "EXAT",
								fmt::to_string(in_100s)}) == "+OK\r\n");
			REQUIRE(server.run({"SET", "b", "v", "PXAT",
								fmt::to_string(in_100s * 1000)}) == "+OK\r\n");

			THEN("both expire at the absolute time") {
				// Up to a second less than 100 s, as now was rounded down.
				CHECK(pttl(server, "a") > 98'000);
				CHECK(pttl(server, "a") <= 100'000);
				CHECK(pttl(server, "b") > 98'000);
				CHECK(pttl(server, "b") <= 100'000);
			}
		}
	}
}

// TC-333. Waits on the real clock: excluded under the sanitizers (DX-93).
SCENARIO("A PX deadline passes on the real clock", "[commands][set][timing]") {
	GIVEN("a key set with PX 50") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v", "PX", "50"}) == "+OK\r\n");

		WHEN("well past the deadline") {
			std::this_thread::sleep_for(300ms);

			THEN("the key is gone") {
				CHECK(server.run({"GET", "k"}) == "$-1\r\n");
				CHECK(server.run({"PTTL", "k"}) == ":-2\r\n");
			}
		}
	}
}

SCENARIO("SET handles an existing TTL like Redis",
		 "[commands][set]") {
	GIVEN("a key with a TTL") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v", "EX", "100"}) == "+OK\r\n");

		WHEN("TC-094: it is overwritten by a plain SET") {
			REQUIRE(server.run({"SET", "k", "v2"}) == "+OK\r\n");
			THEN("the TTL is removed") {
				CHECK(server.run({"TTL", "k"}) == ":-1\r\n");
			}
		}

		WHEN("TC-095: it is overwritten with KEEPTTL") {
			REQUIRE(server.run({"SET", "k", "v2", "KEEPTTL"}) == "+OK\r\n");
			THEN("the TTL is kept and the key stays readable") {
				CHECK(server.run({"GET", "k"}) == "$2\r\nv2\r\n");
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
			}
		}
	}
}

SCENARIO("SET option validation",
		 "[commands][set]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("TC-096: options conflict or are incomplete") {
			THEN("the command is a syntax error") {
				CHECK(server.run({"SET", "k", "v", "NX", "XX"}) ==
					  "-ERR syntax error\r\n");
				CHECK(server.run({"SET", "k", "v", "EX", "1", "PX", "1"}) ==
					  "-ERR syntax error\r\n");
				CHECK(server.run({"SET", "k", "v", "EX"}) ==
					  "-ERR syntax error\r\n");
				CHECK(server.run({"SET", "k", "v", "BOGUS"}) ==
					  "-ERR syntax error\r\n");
			}
		}

		WHEN("TC-102: the expire time is not positive") {
			THEN("it is rejected") {
				CHECK(server.run({"SET", "k", "v", "EX", "0"}) ==
					  "-ERR invalid expire time in 'set' command\r\n");
			}
		}

		WHEN("TC-097: an option is repeated") {
			THEN("it is allowed and the last expiry wins, as in Redis") {
				CHECK(server.run({"SET", "k", "v", "EX", "100000", "EX", "0"}) ==
					  "-ERR invalid expire time in 'set' command\r\n");
				CHECK(server.run({"SET", "k", "v", "EX", "0", "EX", "100"}) ==
					  "+OK\r\n");
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
				CHECK(server.run({"SET", "k", "w", "KEEPTTL", "KEEPTTL"}) ==
					  "+OK\r\n");
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
			}
		}

		WHEN("TC-096: a syntax error follows a bad expire time") {
			THEN("the syntax error is reported, as in Redis") {
				CHECK(server.run({"SET", "k", "v", "EX", "0", "BOGUS"}) ==
					  "-ERR syntax error\r\n");
				CHECK(server.run({"SET", "k", "v", "EX", "abc", "KEEPTTL"}) ==
					  "-ERR syntax error\r\n");
			}
		}
	}
}

SCENARIO("SET with NX, XX and GET", "[commands][set]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("XX, then NX twice, are used on the same key") {
			const auto xx        = server.run({"SET", "k", "v", "XX"});
			const auto first_nx  = server.run({"SET", "k", "v", "NX"});
			const auto second_nx = server.run({"SET", "k", "v2", "NX"});

			THEN("XX skips the missing key and only the first NX writes") {
				CHECK(xx == "$-1\r\n");
				CHECK(first_nx == "+OK\r\n");
				CHECK(second_nx == "$-1\r\n");
				CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}

		WHEN("SET ... GET is used twice") {
			const auto first  = server.run({"SET", "k", "old", "GET"});
			const auto second = server.run({"SET", "k", "new", "GET"});

			THEN("each reply is the previous value") {
				CHECK(first == "$-1\r\n");
				CHECK(second == "$3\r\nold\r\n");
				CHECK(server.run({"GET", "k"}) == "$3\r\nnew\r\n");
			}
		}
	}
}

SCENARIO("Malformed requests get RESP error replies", "[commands]") {
	GIVEN("a command handler") {
		test::Server server;
		const CommandHandler handler;

		WHEN("the request is an empty array") {
			const auto reply = handler.execute(
				server.ctx, resp::Value::from_array(resp::Value::Array{}));
			THEN("it is rejected instead of indexing past the end") {
				CHECK_THAT(reply, StartsWith("-ERR Protocol error"));
				CHECK_THAT(reply, EndsWith("\r\n"));
			}
		}

		WHEN("an argument is not a string") {
			const auto reply = handler.execute(
				server.ctx,
				resp::Value::from_array({resp::Value::from_bulk_string("GET"),
										 resp::Value::from_integer(1)}));
			THEN("it is rejected") {
				CHECK_THAT(reply, StartsWith("-ERR Protocol error"));
			}
		}

		WHEN("the command is unknown or has the wrong arity") {
			THEN("the errors match Redis' wording") {
				CHECK(server.run({"NOPE"}) ==
					  "-ERR unknown command 'NOPE', with args beginning with: \r\n");
				CHECK(server.run({"NOPE", "a", "b"}) ==
					  "-ERR unknown command 'NOPE', with args beginning with: "
					  "'a' 'b' \r\n");
				CHECK(server.run({"GET", "a", "b"}) ==
					  "-ERR wrong number of arguments for 'get' command\r\n");
			}
		}
	}
}

SCENARIO("Command names are matched without regard to case",
		 "[commands][dispatch]") {
	GIVEN("a keyspace holding one key") {
		test::Server server;
		REQUIRE(server.run({"set", "k", "v"}) == "+OK\r\n");

		THEN("lower and mixed case names find their command") {
			CHECK(server.run({"get", "k"}) == "$1\r\nv\r\n");
			CHECK(server.run({"GeT", "k"}) == "$1\r\nv\r\n");
			CHECK(server.run({"strlen", "k"}) == ":1\r\n");
		}

		THEN("an arity error names the command in lower case") {
			CHECK(server.run({"GET"}) ==
				  "-ERR wrong number of arguments for 'get' command\r\n");
		}

		THEN("an unknown name is echoed back as the client sent it") {
			CHECK(server.run({"NoSuchCmd"}) ==
				  "-ERR unknown command 'NoSuchCmd', with args beginning with: \r\n");
			CHECK(server.run({""}) ==
				  "-ERR unknown command '', with args beginning with: \r\n");
		}

		THEN("a name longer than any command is unknown, not truncated") {
			const std::string longer(40, 'a');
			CHECK(server.run({longer}) ==
				  fmt::format("-ERR unknown command '{}', with args beginning "
							  "with: \r\n",
							  longer));
			// A long name must not match a real command by prefix.
			const std::string long_get = fmt::format("GET{}", std::string(40, 'x'));
			CHECK(server.run({long_get}) ==
				  fmt::format("-ERR unknown command '{}', with args beginning "
							  "with: \r\n",
							  long_get));
		}

		THEN("the echoed name and arguments stop at 128 bytes, as in Redis") {
			const std::string name(200, 'n');
			const std::string arg(100, 'a');
			// The second argument gets only what is left of the 128: 128 - 103.
			CHECK(server.run({name, arg, arg, arg}) ==
				  fmt::format("-ERR unknown command '{}', with args beginning "
							  "with: '{}' '{}' \r\n",
							  std::string(128, 'n'),
							  arg,
							  std::string(25, 'a')));
		}
	}
}

SCENARIO("A reply is appended to the caller's buffer", "[commands][dispatch]") {
	GIVEN("a handler and a buffer that already holds a reply") {
		static const CommandHandler handler;
		test::Server server;
		std::string out = "+earlier\r\n";

		WHEN("two requests run against it") {
			const std::vector<std::string> ping{"PING"};
			const std::vector<std::string> set{"SET", "k", "v"};
			handler.execute(server.ctx, resp::Value::from_raw_array(ping), out);
			handler.execute(server.ctx, resp::Value::from_raw_array(set), out);

			THEN("their replies follow, in order, and nothing is lost") {
				CHECK(out == "+earlier\r\n+PONG\r\n+OK\r\n");
			}
		}
	}
}

SCENARIO("A plain SET overwrites without looking at the old value",
		 "[commands][set]") {
	GIVEN("a key holding a list") {
		test::Server server;
		REQUIRE(server.run({"LPUSH", "k", "a"}) == ":1\r\n");

		WHEN("SET is run without options") {
			CHECK(server.run({"SET", "k", "v"}) == "+OK\r\n");

			THEN("the list is replaced by the string") {
				CHECK(server.run({"TYPE", "k"}) == "+string\r\n");
				CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}

		WHEN("SET is run with NX, XX or GET") {
			THEN("the old value still decides the outcome") {
				CHECK(server.run({"SET", "k", "v", "NX"}) == "$-1\r\n");
				CHECK(server.run({"SET", "k", "v", "GET"}) ==
					  "-WRONGTYPE Operation against a key holding the wrong "
					  "kind of value\r\n");
				CHECK(server.run({"SET", "k", "v", "XX"}) == "+OK\r\n");
			}
		}
	}

	GIVEN("a key with a TTL") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v", "EX", "100"}) == "+OK\r\n");

		WHEN("SET is run without options") {
			REQUIRE(server.run({"SET", "k", "w"}) == "+OK\r\n");

			THEN("the TTL is cleared, as in Redis") {
				CHECK(server.run({"TTL", "k"}) == ":-1\r\n");
			}
		}

		WHEN("SET is run with KEEPTTL") {
			REQUIRE(server.run({"SET", "k", "w", "KEEPTTL"}) == "+OK\r\n");

			THEN("the TTL is kept") {
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
			}
		}
	}

	GIVEN("a keyspace with no deadlines at all") {
		test::Server server;

		THEN("SET, GET and DEL still work on it") {
			CHECK(server.run({"SET", "a", "1"}) == "+OK\r\n");
			CHECK(server.run({"GET", "a"}) == "$1\r\n1\r\n");
			CHECK(server.run({"DEL", "a"}) == ":1\r\n");
			CHECK(server.run({"GET", "a"}) == "$-1\r\n");
		}
	}
}

SCENARIO("TC-101, TC-102: Expiry arguments that overflow the clock are rejected",
		 "[commands][set][limits]") {
	test::Server server;
	const std::string huge = "9223372036854775807";
	// Seconds cannot be scaled to milliseconds, and a relative amount cannot be
	// added to the current time, without overflowing.
	for (const char *opt : {"EX", "PX", "EXAT"})
		CHECK(server.run({"SET", "k", "v", opt, huge}) ==
			  "-ERR invalid expire time in 'set' command\r\n");
	server.run({"SET", "k", "v"});
	// Redis names the command, in lower case (#44).
	for (const auto &[cmd, name] :
		 {std::pair{"EXPIRE", "expire"},
		  std::pair{"PEXPIRE", "pexpire"},
		  std::pair{"EXPIREAT", "expireat"}})
		CHECK(server.run({cmd, "k", huge}) ==
			  fmt::format("-ERR invalid expire time in '{}' command\r\n", name));
	CHECK(server.run({"EXPIRE", "k", "-9223372036854775808"}) ==
		  "-ERR invalid expire time in 'expire' command\r\n");
	CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
	CHECK(server.run({"TTL", "k"}) == ":-1\r\n");

	// An absolute time in milliseconds needs no arithmetic, so it is valid, as
	// in Redis: the key simply never expires.
	CHECK(server.run({"PEXPIREAT", "k", huge}) == ":1\r\n");
	CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
	CHECK(server.run({"SET", "j", "v", "PXAT", huge}) == "+OK\r\n");
	CHECK(server.run({"GET", "j"}) == "$1\r\nv\r\n");
}
