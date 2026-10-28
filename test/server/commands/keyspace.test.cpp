#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace redis;
using namespace std::chrono_literals;

SCENARIO("Deleting and checking keys", "[commands][keyspace]") {
	GIVEN("two keys") {
		test::Server server;
		REQUIRE(server.run({"SET", "a", "1"}) == "+OK\r\n");
		REQUIRE(server.run({"SET", "b", "2"}) == "+OK\r\n");

		THEN("EXISTS counts the existing keys") {
			CHECK(server.run({"EXISTS", "a", "b", "missing"}) == ":2\r\n");
			CHECK(server.run({"DBSIZE"}) == ":2\r\n");
		}

		WHEN("one existing and one missing key are deleted") {
			const auto removed = server.run({"DEL", "a", "missing"});

			THEN("only the existing key counts") {
				CHECK(removed == ":1\r\n");
				CHECK(server.run({"EXISTS", "a"}) == ":0\r\n");
			}
		}

		WHEN("the database is flushed") {
			REQUIRE(server.run({"SET", "c", "3", "EX", "100"}) == "+OK\r\n");
			const auto reply = server.run({"FLUSHDB"});

			THEN("every key and TTL is gone") {
				CHECK(reply == "+OK\r\n");
				CHECK(server.run({"DBSIZE"}) == ":0\r\n");
				CHECK(server.run({"TTL", "c"}) == ":-2\r\n");
			}
		}
	}
}

SCENARIO("Listing keys by pattern", "[commands][keyspace]") {
	GIVEN("keys in two namespaces") {
		test::Server server;
		REQUIRE(server.run({"MSET", "user:1", "a", "user:2", "b", "post:1",
							"c"}) == "+OK\r\n");

		WHEN("keys matching user:* are listed") {
			const auto reply = server.run({"KEYS", "user:*"});

			THEN("only that namespace is returned") {
				CHECK(reply.starts_with("*2\r\n"));
				CHECK(reply.contains("user:1"));
				CHECK(reply.contains("user:2"));
				CHECK_FALSE(reply.contains("post:1"));
			}
		}
	}
}

SCENARIO("Setting and clearing expiry", "[commands][keyspace][ttl]") {
	GIVEN("a persistent key") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v"}) == "+OK\r\n");

		THEN("TTL and PTTL report -1, and -2 for missing keys") {
			CHECK(server.run({"TTL", "k"}) == ":-1\r\n");
			CHECK(server.run({"PTTL", "k"}) == ":-1\r\n");
			CHECK(server.run({"TTL", "missing"}) == ":-2\r\n");
			CHECK(server.run({"PTTL", "missing"}) == ":-2\r\n");
		}

		WHEN("EXPIRE sets a TTL") {
			REQUIRE(server.run({"EXPIRE", "k", "100"}) == ":1\r\n");

			THEN("TTL reports it") {
				CHECK(server.run({"TTL", "k"}) == ":100\r\n");
			}

			AND_WHEN("PERSIST removes it") {
				const auto reply = server.run({"PERSIST", "k"});

				THEN("the key is persistent again") {
					CHECK(reply == ":1\r\n");
					CHECK(server.run({"TTL", "k"}) == ":-1\r\n");
				}
			}
		}

		WHEN("PEXPIRE sets a short TTL that passes") {
			REQUIRE(server.run({"PEXPIRE", "k", "20"}) == ":1\r\n");
			std::this_thread::sleep_for(40ms);

			THEN("the key is gone") {
				CHECK(server.run({"EXISTS", "k"}) == ":0\r\n");
			}
		}

		WHEN("EXPIREAT names a time in the past") {
			const auto reply = server.run({"EXPIREAT", "k", "1"});

			THEN("the key is deleted at once") {
				CHECK(reply == ":1\r\n");
				CHECK(server.run({"EXISTS", "k"}) == ":0\r\n");
			}
		}

		WHEN("EXPIRE targets a missing key or a bad number") {
			THEN("it reports 0 or an error") {
				CHECK(server.run({"EXPIRE", "missing", "10"}) == ":0\r\n");
				CHECK(server.run({"EXPIRE", "k", "soon"}) ==
					  "-ERR value is not an integer or out of range\r\n");
			}
		}
	}
}
