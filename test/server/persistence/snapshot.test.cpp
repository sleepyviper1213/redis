#include "../commands/commands.fixture.hpp"
#include "server/persistence/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <thread>

using namespace redis;
using namespace std::chrono_literals;

SCENARIO("Snapshots round-trip every value type", "[persistence]") {
	GIVEN("a keyspace with one key of each type and a TTL") {
		test::Server server;
		REQUIRE(server.run({"SET", "s", "hello", "EX", "100"}) == "+OK\r\n");
		REQUIRE(server.run({"RPUSH", "l", "a", "b", "c"}) == ":3\r\n");
		REQUIRE(server.run({"SADD", "set", "x", "y"}) == ":2\r\n");
		REQUIRE(server.run({"HSET", "h", "f", "v"}) == ":1\r\n");
		REQUIRE(server.run({"SET", "binary", std::string("a\0b\r\n", 5)}) ==
				"+OK\r\n");

		WHEN("it is saved and loaded into a fresh server") {
			REQUIRE(persistence::save_snapshot(server.db, server.dump.path()));
			test::Server restored;
			REQUIRE(persistence::load_snapshot(restored.db, server.dump.path()));

			THEN("every value comes back") {
				CHECK(restored.run({"DBSIZE"}) == ":5\r\n");
				CHECK(restored.run({"GET", "s"}) == "$5\r\nhello\r\n");
				CHECK(restored.run({"LRANGE", "l", "0", "-1"}) ==
					  "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
				CHECK(restored.run({"SISMEMBER", "set", "y"}) == ":1\r\n");
				CHECK(restored.run({"HGET", "h", "f"}) == "$1\r\nv\r\n");
				CHECK(restored.run({"STRLEN", "binary"}) == ":5\r\n");
			}

			THEN("the TTL is kept as an absolute deadline") {
				CHECK(restored.run({"TTL", "s"}) == ":100\r\n");
				CHECK(restored.run({"TTL", "l"}) == ":-1\r\n");
			}
		}
	}

	GIVEN("a key whose TTL passes after the snapshot is written") {
		test::Server server;
		REQUIRE(server.run({"SET", "short", "v", "PX", "30"}) == "+OK\r\n");
		REQUIRE(server.run({"SET", "kept", "v"}) == "+OK\r\n");
		REQUIRE(persistence::save_snapshot(server.db, server.dump.path()));
		std::this_thread::sleep_for(50ms);

		WHEN("the snapshot is loaded") {
			test::Server restored;
			REQUIRE(persistence::load_snapshot(restored.db, server.dump.path()));

			THEN("the expired key is not resurrected") {
				CHECK(restored.run({"EXISTS", "short"}) == ":0\r\n");
				CHECK(restored.run({"EXISTS", "kept"}) == ":1\r\n");
			}
		}
	}
}

SCENARIO("Loading rejects files that are not valid snapshots",
		 "[persistence]") {
	GIVEN("a keyspace that already holds data") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v"}) == "+OK\r\n");

		WHEN("the snapshot file does not exist") {
			const auto result =
				persistence::load_snapshot(server.db, server.dump.path());

			THEN("loading succeeds and leaves the keyspace alone") {
				CHECK(result.has_value());
				CHECK(server.run({"DBSIZE"}) == ":1\r\n");
			}
		}

		WHEN("the file is not a snapshot") {
			std::ofstream(server.dump.path()) << "definitely not a snapshot";
			const auto result =
				persistence::load_snapshot(server.db, server.dump.path());

			THEN("loading fails and the keyspace is untouched") {
				CHECK_FALSE(result.has_value());
				CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}

		WHEN("a valid snapshot is truncated") {
			REQUIRE(server.run({"RPUSH", "l", "a", "b", "c"}) == ":3\r\n");
			REQUIRE(persistence::save_snapshot(server.db, server.dump.path()));
			const auto size = std::filesystem::file_size(server.dump.path());
			std::filesystem::resize_file(server.dump.path(), size - 3);

			test::Server restored;
			const auto result =
				persistence::load_snapshot(restored.db, server.dump.path());

			THEN("loading fails instead of loading partial data") {
				CHECK_FALSE(result.has_value());
				CHECK(restored.run({"DBSIZE"}) == ":0\r\n");
			}
		}
	}
}

SCENARIO("Saving through commands", "[persistence][commands]") {
	GIVEN("a keyspace with data") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v"}) == "+OK\r\n");

		WHEN("SAVE is called") {
			const auto before = server.run({"LASTSAVE"});
			std::this_thread::sleep_for(1100ms); // LASTSAVE has 1s resolution
			const auto reply = server.run({"SAVE"});

			THEN("the file is written and LASTSAVE advances") {
				CHECK(reply == "+OK\r\n");
				CHECK(std::filesystem::exists(server.dump.path()));
				CHECK(server.run({"LASTSAVE"}) != before);
			}
		}

		WHEN("CONFIG GET asks for the snapshot file name") {
			const auto reply = server.run({"CONFIG", "GET", "dbfilename"});

			THEN("it reports the configured file") {
				CHECK(reply.starts_with("*2\r\n$10\r\ndbfilename\r\n"));
				CHECK(reply.contains(server.dump.path().filename().string()));
			}
		}
	}
}

SCENARIO("Saving in the background", "[persistence][commands][fork]") {
	GIVEN("a keyspace with data") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v"}) == "+OK\r\n");

		WHEN("BGSAVE is called and the child finishes") {
			const auto reply   = server.run({"BGSAVE"});
			const auto overlap = server.run({"BGSAVE"});
			for (int i = 0; i < 100 &&
							server.persistence.background_save_in_progress();
				 ++i) {
				std::this_thread::sleep_for(10ms);
				server.persistence.poll();
			}

			THEN("one save ran in a child process and wrote the snapshot") {
				CHECK(reply == "+Background saving started\r\n");
				CHECK(overlap ==
					  "-ERR Background save already in progress\r\n");
				CHECK_FALSE(server.persistence.background_save_in_progress());

				test::Server restored;
				REQUIRE(persistence::load_snapshot(restored.db,
												   server.dump.path()));
				CHECK(restored.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}
	}
}
