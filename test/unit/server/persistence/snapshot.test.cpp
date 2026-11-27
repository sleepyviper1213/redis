#include "server/persistence/detail/snapshot.hpp"

#include "../commands/commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <fstream>
#include <thread>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

using namespace redis;
using namespace std::chrono_literals;
using persistence::PersistKind;

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
			REQUIRE(persistence::detail::save_snapshot(server.db,
													   server.dump.path()));
			test::Server restored;
			REQUIRE(persistence::detail::load_snapshot(restored.db,
													   server.dump.path()));

			THEN("every value comes back") {
				CHECK(restored.run({"DBSIZE"}) == ":5\r\n");
				CHECK(restored.run({"GET", "s"}) == "$5\r\nhello\r\n");
				CHECK(restored.run({"LRANGE", "l", "0", "-1"}) ==
					  "*3\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
				CHECK(restored.run({"SISMEMBER", "set", "y"}) == ":1\r\n");
				CHECK(restored.run({"HGET", "h", "f"}) == "$1\r\nv\r\n");
				CHECK(restored.run({"STRLEN", "binary"}) == ":5\r\n");
			}

			THEN("the TTL is kept, and the deadline is the same instant") {
				CHECK(restored.run({"TTL", "s"}) == ":100\r\n");
				CHECK(restored.db.expiry("s") == server.db.expiry("s"));
				CHECK(restored.run({"TTL", "l"}) == ":-1\r\n");
			}
		}
	}

	GIVEN("keys and values that are not UTF-8") {
		test::Server server;
		const std::string key("k\xff\xfe", 3);
		const std::string bytes("\x80\x00\xc3", 3);
		REQUIRE(server.run({"SET", key, bytes}) == "+OK\r\n");
		REQUIRE(server.run({"RPUSH", "l", bytes}) == ":1\r\n");
		REQUIRE(server.run({"HSET", "h", bytes, bytes}) == ":1\r\n");

		WHEN("they are saved and loaded") {
			REQUIRE(persistence::detail::save_snapshot(server.db,
													   server.dump.path()));
			test::Server restored;
			REQUIRE(persistence::detail::load_snapshot(restored.db,
													   server.dump.path()));

			THEN("every byte comes back") {
				CHECK(restored.run({"GET", key}) == "$3\r\n" + bytes + "\r\n");
				CHECK(restored.run({"LINDEX", "l", "0"}) ==
					  "$3\r\n" + bytes + "\r\n");
				CHECK(restored.run({"HGET", "h", bytes}) ==
					  "$3\r\n" + bytes + "\r\n");
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
				persistence::detail::load_snapshot(server.db,
												   server.dump.path());

			THEN("loading succeeds and leaves the keyspace alone") {
				CHECK(result.has_value());
				CHECK(server.run({"DBSIZE"}) == ":1\r\n");
			}
		}

		WHEN("the file is not a snapshot") {
			std::ofstream(server.dump.path()) << "definitely not a snapshot";
			const auto result =
				persistence::detail::load_snapshot(server.db,
												   server.dump.path());

			THEN("loading fails and the keyspace is untouched") {
				CHECK_FALSE(result.has_value());
				CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}

		WHEN("a valid snapshot is truncated") {
			REQUIRE(server.run({"RPUSH", "l", "a", "b", "c"}) == ":3\r\n");
			REQUIRE(persistence::detail::save_snapshot(server.db,
													   server.dump.path()));
			const auto size = std::filesystem::file_size(server.dump.path());
			std::filesystem::resize_file(server.dump.path(), size - 3);

			test::Server restored;
			const auto result =
				persistence::detail::load_snapshot(restored.db,
												   server.dump.path());

			THEN("loading fails instead of loading partial data") {
				CHECK_FALSE(result.has_value());
				CHECK(restored.run({"DBSIZE"}) == ":0\r\n");
			}
		}

		WHEN("the file is a version 1 snapshot") {
			std::ofstream out(server.dump.path(), std::ios::binary);
			out.write("RCPPSNAP\x01\x00\x00\x00", 12);
			out.close();
			const auto result =
				persistence::detail::load_snapshot(server.db,
												   server.dump.path());

			THEN("loading reports the version and leaves the keyspace alone") {
				REQUIRE_FALSE(result.has_value());
				CHECK(result.error().kind == PersistKind::SnapshotVersion);
				CHECK(result.error().at == 1);
				CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}

		WHEN("an entry's record is not valid CBOR for a record") {
			REQUIRE(persistence::detail::save_snapshot(server.db,
													   server.dump.path()));
			// The first record starts after the 20-byte header and its 8-byte
			// size; a CBOR "null" is not a map.
			{
				std::fstream file(server.dump.path(),
								  std::ios::binary | std::ios::in |
									  std::ios::out);
				file.seekp(28);
				file.put('\xf6');
			}
			test::Server restored;
			const auto result =
				persistence::detail::load_snapshot(restored.db,
												   server.dump.path());

			THEN("loading fails as corrupt") {
				REQUIRE_FALSE(result.has_value());
				CHECK(result.error().kind == PersistKind::SnapshotCorrupt);
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
			const auto reply = server.run({"SAVE"});

			THEN("the file is written") {
				CHECK(reply == "+OK\r\n");
				CHECK(std::filesystem::exists(server.dump.path()));
			}
		}

		WHEN("CONFIG GET asks for the snapshot file name") {
			const auto reply = server.run({"CONFIG", "GET", "dbfilename"});

			THEN("it reports the configured file") {
				CHECK_THAT(reply, StartsWith("*2\r\n$10\r\ndbfilename\r\n"));
				CHECK_THAT(
					reply,
					ContainsSubstring(server.dump.path().filename().string()));
			}
		}
	}
}

SCENARIO("Saving in the background", "[persistence][commands][fork]") {
#ifdef _WIN32
	SKIP("BGSAVE forks, and Windows cannot");
#endif
	GIVEN("a keyspace with data") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v"}) == "+OK\r\n");

		WHEN("BGSAVE is called and the child finishes") {
			const auto reply   = server.run({"BGSAVE"});
			const auto overlap = server.run({"BGSAVE"});
			// Up to 10 s: a forked child is slow under ThreadSanitizer (its
			// shadow memory is copy-on-write) and the suite runs in parallel.
			// A passing run leaves the loop as soon as the child is reaped.
			for (int i = 0;
				 i < 1000 && server.persistence.is_background_save_in_progress();
				 ++i) {
				std::this_thread::sleep_for(10ms);
				server.persistence.poll();
			}

			THEN("one save ran in a child process and wrote the snapshot") {
				CHECK(reply == "+Background saving started\r\n");
				CHECK(overlap ==
					  "-ERR Background save already in progress\r\n");
				CHECK_FALSE(server.persistence.is_background_save_in_progress());

				test::Server restored;
				REQUIRE(persistence::detail::load_snapshot(restored.db,
														   server.dump.path()));
				CHECK(restored.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}
	}
}

SCENARIO("A snapshot keeps absolute deadlines",
		 "[persistence][expiry]") {
	GIVEN("a key with a fixed absolute deadline") {
		test::Server server;
		// The year 2100, so it cannot pass during the test.
		constexpr auto DEADLINE_MS = "4102444800000";
		REQUIRE(server.run({"SET", "k", "v", "PXAT", DEADLINE_MS}) == "+OK\r\n");
		REQUIRE(
			persistence::detail::save_snapshot(server.db, server.dump.path()));

		WHEN("TC-117: it is loaded into a fresh server") {
			test::Server restored;
			REQUIRE(persistence::detail::load_snapshot(restored.db,
													   server.dump.path()));

			THEN("the deadline is the same to the millisecond") {
				const auto before = server.db.expiry("k");
				const auto after  = restored.db.expiry("k");
				REQUIRE(before.has_value());
				REQUIRE(after.has_value());
				CHECK(after->to_msecs_since_epoch() ==
					  before->to_msecs_since_epoch());
				CHECK(after->to_msecs_since_epoch() == 4102444800000);
			}
		}
	}
}

// A save writes only live keys, so the deadline must pass between the save and
// the load. Waits on the real clock: excluded under the sanitizers (DX-93).
SCENARIO("A snapshot is loaded after one of its deadlines has passed",
		 "[persistence][expiry][timing]") {
	GIVEN("a snapshot whose short deadline has passed since it was written") {
		test::Server server;
		REQUIRE(server.run({"SET", "short", "v", "PX", "200"}) == "+OK\r\n");
		REQUIRE(server.run({"SET", "kept", "v"}) == "+OK\r\n");
		REQUIRE(
			persistence::detail::save_snapshot(server.db, server.dump.path()));
		std::this_thread::sleep_for(400ms);

		WHEN("TC-119: it is loaded as the base of an append-only file") {
			test::Server restored;
			REQUIRE(persistence::detail::load_snapshot(restored.db,
													   server.dump.path(),
													   /*is_keeping_expired=*/true));

			THEN("the key is still there, with its deadline") {
				CHECK(restored.db.size() == 2);
				CHECK(restored.db.expiring() == 1);
			}

			AND_THEN("it expires like any other once a command looks at it") {
				CHECK(restored.run({"EXISTS", "short"}) == ":0\r\n");
				CHECK(restored.db.expired_keys() == 1);
			}
		}

		WHEN("TC-118: it is loaded as a plain snapshot") {
			test::Server restored;
			REQUIRE(persistence::detail::load_snapshot(restored.db,
													   server.dump.path()));

			THEN("the expired key is not resurrected at all") {
				CHECK(restored.db.size() == 1);
				CHECK(restored.db.expiring() == 0);
				CHECK(restored.run({"EXISTS", "short"}) == ":0\r\n");
				CHECK(restored.run({"EXISTS", "kept"}) == ":1\r\n");
			}
		}
	}
}

// Waits on the real clock: excluded under the sanitizers (DX-93).
SCENARIO("SAVE moves LASTSAVE on", "[persistence][commands][timing]") {
	GIVEN("a server that has not saved since it started") {
		test::Server server;
		REQUIRE(server.run({"SET", "k", "v"}) == "+OK\r\n");
		const auto before = server.run({"LASTSAVE"});

		WHEN("SAVE is called more than a second later") {
			std::this_thread::sleep_for(1100ms); // LASTSAVE has 1 s resolution
			REQUIRE(server.run({"SAVE"}) == "+OK\r\n");

			THEN("LASTSAVE has moved") {
				CHECK(server.run({"LASTSAVE"}) != before);
			}
		}
	}
}
