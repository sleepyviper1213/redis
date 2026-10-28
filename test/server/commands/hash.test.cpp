#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace redis;

SCENARIO("Writing and reading hash fields", "[commands][hash]") {
	GIVEN("an empty keyspace") {
		test::Server server;

		WHEN("two fields are set") {
			const auto added = server.run({"HSET", "h", "f1", "v1", "f2", "v2"});

			THEN("both are new and readable") {
				CHECK(added == ":2\r\n");
				CHECK(server.run({"HGET", "h", "f1"}) == "$2\r\nv1\r\n");
				CHECK(server.run({"HLEN", "h"}) == ":2\r\n");
				CHECK(server.run({"HEXISTS", "h", "f2"}) == ":1\r\n");
				CHECK(server.run({"HMGET", "h", "f1", "nope"}) ==
					  "*2\r\n$2\r\nv1\r\n$-1\r\n");
			}

			AND_WHEN("an existing field is overwritten") {
				const auto again = server.run({"HSET", "h", "f1", "new"});

				THEN("it is not counted as added and holds the new value") {
					CHECK(again == ":0\r\n");
					CHECK(server.run({"HGET", "h", "f1"}) == "$3\r\nnew\r\n");
				}
			}

			AND_WHEN("all fields are deleted") {
				const auto removed = server.run({"HDEL", "h", "f1", "f2"});

				THEN("the empty hash is deleted") {
					CHECK(removed == ":2\r\n");
					CHECK(server.run({"EXISTS", "h"}) == ":0\r\n");
				}
			}
		}

		WHEN("HSET is given a field without a value") {
			THEN("it is rejected") {
				CHECK(server.run({"HSET", "h", "f1", "v1", "f2"}) ==
					  "-ERR wrong number of arguments for 'hset' command\r\n");
			}
		}
	}
}
