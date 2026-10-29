#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace redis;
using namespace std::chrono_literals;

SCENARIO("SET applies each expiration option", "[commands][set]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("a key is set with EX") {
			REQUIRE(server.run({"SET", "k", "v", "EX", "100"}) == "+OK\r\n");
			THEN("its TTL is in seconds") {
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
			}
		}

		WHEN("a key is set with PX") {
			REQUIRE(server.run({"SET", "k", "v", "PX", "50"}) == "+OK\r\n");
			THEN("its TTL is in milliseconds") {
				CHECK(server.run({"TTL", "k"}) == ":0\r\n");
			}
			AND_WHEN("the TTL passes") {
				std::this_thread::sleep_for(80ms);
				THEN("the key is gone") {
					CHECK(server.run({"GET", "k"}) == "$-1\r\n");
				}
			}
		}

		WHEN("keys are set with EXAT and PXAT") {
			const auto in_100s =
				std::chrono::duration_cast<std::chrono::seconds>(
					std::chrono::system_clock::now().time_since_epoch())
					.count() +
				100;
			REQUIRE(server.run({"SET", "a", "v", "EXAT",
								std::to_string(in_100s)}) == "+OK\r\n");
			REQUIRE(server.run({"SET", "b", "v", "PXAT",
								std::to_string(in_100s * 1000)}) == "+OK\r\n");

			THEN("both expire at the absolute time") {
				// in_100s was truncated to a whole second, so up to a second
				// has already elapsed: TTL rounds to 99 or 100.
				const auto ttl_a = server.run({"TTL", "a"});
				const auto ttl_b = server.run({"TTL", "b"});
				CHECK((ttl_a == ":100\r\n" || ttl_a == ":99\r\n"));
				CHECK((ttl_b == ":100\r\n" || ttl_b == ":99\r\n"));
			}
		}
	}
}

SCENARIO("SET handles an existing TTL like Redis", "[commands][set]") {
	GIVEN("a key with a TTL") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v", "EX", "100"}) == "+OK\r\n");

		WHEN("it is overwritten by a plain SET") {
			REQUIRE(server.run({"SET", "k", "v2"}) == "+OK\r\n");
			THEN("the TTL is removed") {
				CHECK(server.run({"TTL", "k"}) == ":-1\r\n");
			}
		}

		WHEN("it is overwritten with KEEPTTL") {
			REQUIRE(server.run({"SET", "k", "v2", "KEEPTTL"}) == "+OK\r\n");
			THEN("the TTL is kept and the key stays readable") {
				CHECK(server.run({"GET", "k"}) == "$2\r\nv2\r\n");
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
			}
		}
	}
}

SCENARIO("SET option validation", "[commands][set]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("options conflict or are incomplete") {
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

		WHEN("the expire time is not positive") {
			THEN("it is rejected") {
				CHECK(server.run({"SET", "k", "v", "EX", "0"}) ==
					  "-ERR invalid expire time in 'set' command\r\n");
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
				CHECK(reply.starts_with("-ERR Protocol error"));
				CHECK(reply.ends_with("\r\n"));
			}
		}

		WHEN("an argument is not a string") {
			const auto reply = handler.execute(
				server.ctx,
				resp::Value::from_array({resp::Value::from_bulk_string("GET"),
										 resp::Value::from_integer(1)}));
			THEN("it is rejected") {
				CHECK(reply.starts_with("-ERR Protocol error"));
			}
		}

		WHEN("the command is unknown or has the wrong arity") {
			THEN("the errors match Redis' wording") {
				CHECK(server.run({"NOPE"}) ==
					  "-ERR unknown command 'NOPE'\r\n");
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
				  "-ERR unknown command 'NoSuchCmd'\r\n");
			CHECK(server.run({""}) == "-ERR unknown command ''\r\n");
		}

		THEN("a name longer than any command is unknown, not truncated") {
			const std::string longer(40, 'a');
			CHECK(server.run({longer}) ==
				  "-ERR unknown command '" + longer + "'\r\n");
			// A long name must not match a real command by prefix.
			CHECK(server.run({"GET" + std::string(40, 'x')}) ==
				  "-ERR unknown command 'GET" + std::string(40, 'x') + "'\r\n");
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
