#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace redis;

SCENARIO("Adding and removing set members", "[commands][set]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("members are added, some twice") {
			const auto added = server.run({"SADD", "s", "a", "b", "a"});

			THEN("duplicates are counted once") {
				CHECK(added == ":2\r\n");
				CHECK(server.run({"SCARD", "s"}) == ":2\r\n");
				CHECK(server.run({"SISMEMBER", "s", "a"}) == ":1\r\n");
				CHECK(server.run({"SISMEMBER", "s", "z"}) == ":0\r\n");
			}

			AND_WHEN("every member is removed") {
				const auto removed = server.run({"SREM", "s", "a", "b", "z"});

				THEN("only existing members count and the set is deleted") {
					CHECK(removed == ":2\r\n");
					CHECK(server.run({"EXISTS", "s"}) == ":0\r\n");
				}
			}
		}
	}
}

SCENARIO("Intersecting sets", "[commands][set]") {
	GIVEN("two overlapping sets") {
		test::Server server;
		REQUIRE(server.run({"SADD", "x", "a", "b", "c"}) == ":3\r\n");
		REQUIRE(server.run({"SADD", "y", "b", "c", "d"}) == ":3\r\n");

		WHEN("they are intersected") {
			const auto both = server.run({"SINTER", "x", "y"});

			THEN("the shared members are returned") {
				CHECK(both.starts_with("*2\r\n"));
				CHECK(both.contains("$1\r\nb\r\n"));
				CHECK(both.contains("$1\r\nc\r\n"));
			}
		}

		WHEN("one of the keys is missing") {
			THEN("the intersection is empty") {
				CHECK(server.run({"SINTER", "x", "missing"}) == "*0\r\n");
			}
		}
	}
}

SCENARIO("Popping random members", "[commands][set]") {
	GIVEN("a set of three members") {
		test::Server server;
		REQUIRE(server.run({"SADD", "s", "a", "b", "c"}) == ":3\r\n");

		WHEN("one member is popped") {
			const auto popped = server.run({"SPOP", "s"});

			THEN("it is one of the members and is removed") {
				CHECK((popped == "$1\r\na\r\n" || popped == "$1\r\nb\r\n" ||
					   popped == "$1\r\nc\r\n"));
				CHECK(server.run({"SCARD", "s"}) == ":2\r\n");
			}
		}

		WHEN("more members are popped than the set holds") {
			const auto popped = server.run({"SPOP", "s", "5"});

			THEN("all are returned and the set is deleted") {
				CHECK(popped.starts_with("*3\r\n"));
				CHECK(server.run({"EXISTS", "s"}) == ":0\r\n");
			}
		}
	}
}
