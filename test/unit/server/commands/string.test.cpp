#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace redis;

SCENARIO("Counting with INCR and DECR", "[commands][string]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("a missing counter is incremented and decremented") {
			REQUIRE(server.run({"INCR", "n"}) == ":1\r\n");
			REQUIRE(server.run({"INCRBY", "n", "10"}) == ":11\r\n");
			REQUIRE(server.run({"DECRBY", "n", "20"}) == ":-9\r\n");

			THEN("it starts at zero and is stored as a string") {
				CHECK(server.run({"DECR", "n"}) == ":-10\r\n");
				CHECK(server.run({"GET", "n"}) == "$3\r\n-10\r\n");
			}
		}

		WHEN("a non-numeric string is incremented") {
			REQUIRE(server.run({"SET", "s", "abc"}) == "+OK\r\n");

			THEN("it is rejected") {
				CHECK(server.run({"INCR", "s"}) ==
					  "-ERR value is not an integer or out of range\r\n");
			}
		}

		WHEN("an increment would overflow int64") {
			REQUIRE(server.run({"SET", "n", "9223372036854775807"}) ==
					"+OK\r\n");

			THEN("it is rejected and the value is unchanged") {
				CHECK(server.run({"INCR", "n"}) ==
					  "-ERR increment or decrement would overflow\r\n");
				CHECK(server.run({"GET", "n"}) ==
					  "$19\r\n9223372036854775807\r\n");
			}
		}
	}
}

SCENARIO("Setting and getting several keys at once", "[commands][string]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("two keys are set with MSET") {
			REQUIRE(server.run({"MSET", "a", "1", "b", "2"}) == "+OK\r\n");

			THEN("MGET returns them, with nil for missing keys") {
				CHECK(server.run({"MGET", "a", "missing", "b"}) ==
					  "*3\r\n$1\r\n1\r\n$-1\r\n$1\r\n2\r\n");
			}
		}

		WHEN("MSET is given a key without a value") {
			THEN("it is rejected") {
				CHECK(server.run({"MSET", "a", "1", "b"}) ==
					  "-ERR wrong number of arguments for 'mset' command\r\n");
			}
		}
	}
}

SCENARIO("Appending to strings", "[commands][string]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("a missing key is appended to twice") {
			REQUIRE(server.run({"APPEND", "s", "Hello"}) == ":5\r\n");
			const auto length = server.run({"APPEND", "s", " World"});

			THEN("the pieces are joined") {
				CHECK(length == ":11\r\n");
				CHECK(server.run({"STRLEN", "s"}) == ":11\r\n");
				CHECK(server.run({"GET", "s"}) == "$11\r\nHello World\r\n");
			}
		}
	}
}

SCENARIO("Commands reject keys holding another type", "[commands][types]") {
	GIVEN("a key holding a list") {
		test::Server server;
		REQUIRE(server.run({"RPUSH", "l", "x"}) == ":1\r\n");
		const std::string wrongtype =
			"-WRONGTYPE Operation against a key holding the wrong kind of "
			"value\r\n";

		THEN("string, set and hash commands report WRONGTYPE") {
			CHECK(server.run({"GET", "l"}) == wrongtype);
			CHECK(server.run({"INCR", "l"}) == wrongtype);
			CHECK(server.run({"SADD", "l", "a"}) == wrongtype);
			CHECK(server.run({"HSET", "l", "f", "v"}) == wrongtype);
		}

		THEN("TYPE reports it as a list") {
			CHECK(server.run({"TYPE", "l"}) == "+list\r\n");
			CHECK(server.run({"TYPE", "missing"}) == "+none\r\n");
		}

		WHEN("a plain SET overwrites it") {
			REQUIRE(server.run({"SET", "l", "v"}) == "+OK\r\n");

			THEN("the key becomes a string") {
				CHECK(server.run({"TYPE", "l"}) == "+string\r\n");
			}
		}
	}
}

SCENARIO("Integer arguments are parsed the way Redis does",
		 "[commands][string][integer]") {
	GIVEN("a counter") {
		test::Server server;
		REQUIRE(server.run({"SET", "n", "1"}) == "+OK\r\n");
		const std::string not_integer =
			"-ERR value is not an integer or out of range\r\n";

		THEN("plain decimals, with a minus sign, are accepted") {
			CHECK(server.run({"INCRBY", "n", "10"}) == ":11\r\n");
			CHECK(server.run({"INCRBY", "n", "-11"}) == ":0\r\n");
		}

		THEN("a plus sign, spaces, fractions and prefixes are rejected") {
			for (const char *bad :
				 {"+5", " 5", "5 ", "", "1.5", "0x10", "5abc", "--5"}) {
				INFO("argument '" << bad << "'");
				CHECK(server.run({"INCRBY", "n", bad}) == not_integer);
			}
		}

		THEN("a value outside int64 is rejected") {
			CHECK(server.run({"INCRBY", "n", "9223372036854775808"}) ==
				  not_integer);
			CHECK(server.run({"INCRBY", "n", "-9223372036854775809"}) ==
				  not_integer);
		}

		THEN("a stored value that is not an integer is rejected") {
			REQUIRE(server.run({"SET", "s", "+5"}) == "+OK\r\n");
			CHECK(server.run({"INCR", "s"}) == not_integer);
		}
	}
}
