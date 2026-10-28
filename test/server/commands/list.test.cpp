#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace redis;

SCENARIO("Pushing to and popping from lists", "[commands][list]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("elements are pushed to both ends") {
			REQUIRE(server.run({"RPUSH", "l", "b", "c"}) == ":2\r\n");
			REQUIRE(server.run({"LPUSH", "l", "a"}) == ":3\r\n");

			THEN("the list keeps head-to-tail order") {
				CHECK(server.run({"LRANGE", "l", "0", "-1"}) ==
					  "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
				CHECK(server.run({"LLEN", "l"}) == ":3\r\n");
			}

			AND_WHEN("elements are popped from both ends") {
				const auto head = server.run({"LPOP", "l"});
				const auto tail = server.run({"RPOP", "l"});

				THEN("each end yields its element") {
					CHECK(head == "$1\r\na\r\n");
					CHECK(tail == "$1\r\nc\r\n");
				}
			}

			AND_WHEN("more elements are popped than the list holds") {
				const auto popped = server.run({"LPOP", "l", "10"});

				THEN("all are returned and the empty list is deleted") {
					CHECK(popped == "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
					CHECK(server.run({"EXISTS", "l"}) == ":0\r\n");
				}
			}
		}

		WHEN("popping from a missing key") {
			THEN("the reply is nil") {
				CHECK(server.run({"LPOP", "missing"}) == "$-1\r\n");
			}
		}
	}
}

SCENARIO("Reading list ranges and indexes", "[commands][list]") {
	GIVEN("the list [a b c d e]") {
		test::Server server;
		REQUIRE(server.run({"RPUSH", "l", "a", "b", "c", "d", "e"}) == ":5\r\n");

		THEN("negative indexes count from the tail") {
			CHECK(server.run({"LRANGE", "l", "-2", "-1"}) ==
				  "*2\r\n$1\r\nd\r\n$1\r\ne\r\n");
			CHECK(server.run({"LINDEX", "l", "-1"}) == "$1\r\ne\r\n");
		}

		THEN("out-of-range bounds are clamped") {
			CHECK(server.run({"LRANGE", "l", "-100", "1"}) ==
				  "*2\r\n$1\r\na\r\n$1\r\nb\r\n");
			CHECK(server.run({"LRANGE", "l", "3", "100"}) ==
				  "*2\r\n$1\r\nd\r\n$1\r\ne\r\n");
		}

		THEN("empty and inverted ranges are empty") {
			CHECK(server.run({"LRANGE", "l", "10", "20"}) == "*0\r\n");
			CHECK(server.run({"LRANGE", "l", "3", "1"}) == "*0\r\n");
			CHECK(server.run({"LINDEX", "l", "5"}) == "$-1\r\n");
		}

		THEN("non-integer indexes are rejected") {
			CHECK(server.run({"LRANGE", "l", "a", "1"}) ==
				  "-ERR value is not an integer or out of range\r\n");
		}
	}
}
