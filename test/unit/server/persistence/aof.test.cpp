#include "../commands/commands.fixture.hpp"
#include "core/deadline_timer.hpp"
#include "core/resp/value.hpp"
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/persistence/snapshot_manager.hpp"
#include "server/database.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace redis;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

/// A unique directory in the system temp directory, removed on destruction.
class TempDir {
public:
	TempDir() {
		static std::atomic<int> counter{0};
		path_ = fs::temp_directory_path() /
				("redis_aof_test_" + std::to_string(counter++) + "_" +
				 std::to_string(
					 std::chrono::steady_clock::now().time_since_epoch().count()));
	}
	TempDir(const TempDir &)            = delete;
	TempDir &operator=(const TempDir &) = delete;
	~TempDir() {
		std::error_code ignored;
		fs::remove_all(path_, ignored);
	}
	[[nodiscard]] const fs::path &path() const { return path_; }

private:
	fs::path path_;
};

/// A keyspace logging to (and loaded from) the append-only files in \p dir.
/// Constructing another one over the same directory is a restart.
struct AofServer {
	explicit AofServer(const fs::path &dir, const fs::path &snapshot,
					   FsyncPolicy fsync = FsyncPolicy::no)
		: persistence(snapshot),
		  aof(AofConfig{.dir = dir, .fsync = fsync}, snapshot) {}

	/// Replay the files; separate from the constructor so a test can look at
	/// the failure.
	[[nodiscard]] std::expected<void, std::string> start() {
		return aof.load(ctx, handler);
	}

	/// One batch of one command: its reply, with the log written out.
	std::string run(std::vector<std::string> argv) {
		auto reply = handler.execute(ctx, resp::Value::from_raw_array(argv));
		aof.after_batch();
		return reply;
	}

	/// What the newest incremental file holds.
	[[nodiscard]] std::string log() const {
		std::ifstream in(aof.files().back(), std::ios::binary);
		std::ostringstream content;
		content << in.rdbuf();
		return content.str();
	}

	Database db;
	SnapshotManager persistence;
	CommandHandler handler;
	CommandContext ctx{db, persistence};
	AppendOnlyFile aof;
};

/// A command as it appears in the log.
std::string logged(const std::vector<std::string> &argv) {
	std::string out = "*" + std::to_string(argv.size()) + "\r\n";
	for (const auto &arg : argv)
		out += "$" + std::to_string(arg.size()) + "\r\n" + arg + "\r\n";
	return out;
}

/// Replaying the files in \p dir into a fresh keyspace.
struct Restarted : AofServer {
	explicit Restarted(const fs::path &dir, const fs::path &snapshot)
		: AofServer(dir, snapshot) {
		const auto started = start();
		REQUIRE(started.has_value());
	}
};
} // namespace

SCENARIO("Write commands are logged", "[aof]") {
	GIVEN("a server with an append-only file") {
		TempDir dir;
		test::TempFile snapshot;
		AofServer server(dir.path(), snapshot.path());
		REQUIRE(server.start().has_value());

		WHEN("it runs reads, writes and a write that fails") {
			CHECK(server.run({"SET", "a", "1"}) == "+OK\r\n");
			CHECK(server.run({"GET", "a"}) == "$1\r\n1\r\n");
			CHECK(server.run({"INCR", "a"}) == ":2\r\n");
			CHECK(server.run({"LPUSH", "a", "x"}).starts_with("-WRONGTYPE"));
			CHECK(server.run({"DEL", "a"}) == ":1\r\n");

			THEN("only the writes that succeeded are in the log, in order") {
				CHECK(server.log() == logged({"SET", "a", "1"}) +
										  logged({"INCR", "a"}) +
										  logged({"DEL", "a"}));
			}
		}
	}
}

SCENARIO("Commands that cannot be replayed as received", "[aof]") {
	GIVEN("a server with an append-only file") {
		TempDir dir;
		test::TempFile snapshot;
		AofServer server(dir.path(), snapshot.path());
		REQUIRE(server.start().has_value());

		WHEN("SET has a relative expiry") {
			server.run({"SET", "k", "v", "EX", "100"});

			THEN("the log has the absolute deadline") {
				const auto log = server.log();
				CHECK(log.contains("PXAT"));
				CHECK_FALSE(log.contains("EX\r\n"));
			}
		}

		WHEN("EXPIRE is used") {
			server.run({"SET", "k", "v"});
			server.run({"EXPIRE", "k", "100"});

			THEN("the log has PEXPIREAT") {
				const auto log = server.log();
				CHECK(log.contains("PEXPIREAT"));
				CHECK_FALSE(log.contains("\r\nEXPIRE\r\n"));
			}
		}

		WHEN("EXPIRE deletes the key because the time has passed") {
			server.run({"SET", "k", "v"});
			server.run({"PEXPIREAT", "k", "1"});

			THEN("the log has DEL") {
				CHECK(server.log() == logged({"SET", "k", "v"}) +
										  logged({"DEL", "k"}));
			}
		}

		WHEN("SPOP removes random members") {
			server.run({"SADD", "s", "a", "b", "c"});
			const auto popped = server.run({"SPOP", "s", "2"});

			THEN("a replay leaves the same member behind") {
				const auto remaining = server.run({"SMEMBERS", "s"});
				CHECK(server.log().contains("SREM"));
				CHECK_FALSE(server.log().contains("SPOP"));

				Restarted again(dir.path(), snapshot.path());
				CHECK(again.run({"SMEMBERS", "s"}) == remaining);
			}
		}
	}
}

SCENARIO("A restart gets back what was logged", "[aof]") {
	GIVEN("a server that logged every kind of value") {
		TempDir dir;
		test::TempFile snapshot;
		std::string ttl;
		{
			AofServer server(dir.path(), snapshot.path());
			REQUIRE(server.start().has_value());
			server.run({"SET", "str", "hello"});
			server.run({"MSET", "x", "1", "y", "2"});
			server.run({"INCRBY", "x", "41"});
			server.run({"APPEND", "str", " world"});
			server.run({"RPUSH", "list", "a", "b", "c"});
			server.run({"LPOP", "list"});
			server.run({"SADD", "set", "m"});
			server.run({"HSET", "hash", "f", "v"});
			server.run({"SET", "ttl", "v", "EX", "100"});
			server.run({"DEL", "y"});
			ttl = server.run({"PTTL", "ttl"});
		}

		WHEN("another server starts over the same files") {
			Restarted server(dir.path(), snapshot.path());

			THEN("the keyspace is the same") {
				CHECK(server.run({"GET", "str"}) == "$11\r\nhello world\r\n");
				CHECK(server.run({"GET", "x"}) == "$2\r\n42\r\n");
				CHECK(server.run({"EXISTS", "y"}) == ":0\r\n");
				CHECK(server.run({"LRANGE", "list", "0", "-1"}) ==
					  "*2\r\n$1\r\nb\r\n$1\r\nc\r\n");
				CHECK(server.run({"SISMEMBER", "set", "m"}) == ":1\r\n");
				CHECK(server.run({"HGET", "hash", "f"}) == "$1\r\nv\r\n");
			}

			AND_THEN("the deadline is the original, not one from the restart") {
				const auto left = std::stoll(server.run({"PTTL", "ttl"}).substr(1));
				CHECK(left > 90'000);
				CHECK(left <= 100'000);
			}

			AND_THEN("it keeps logging after the old commands") {
				server.run({"SET", "more", "1"});
				Restarted third(dir.path(), snapshot.path());
				CHECK(third.run({"GET", "more"}) == "$1\r\n1\r\n");
				CHECK(third.run({"GET", "str"}) == "$11\r\nhello world\r\n");
			}
		}
	}
}

SCENARIO("A replay does not depend on when it runs", "[aof]") {
	GIVEN("a key that expired before the restart, and one that was persisted") {
		TempDir dir;
		test::TempFile snapshot;
		{
			AofServer server(dir.path(), snapshot.path());
			REQUIRE(server.start().has_value());
			// k is alive for the INCR and then made permanent; by the time of
			// the restart its original deadline has long passed.
			server.run({"SET", "k", "5", "PX", "30"});
			server.run({"INCR", "k"});
			server.run({"PERSIST", "k"});
			// gone is found expired by a read, which logs a DEL; INCR then
			// starts over from 1.
			server.run({"SET", "gone", "5", "PX", "1"});
			std::this_thread::sleep_for(20ms);
			CHECK(server.run({"GET", "gone"}) == "$-1\r\n");
			CHECK(server.run({"INCR", "gone"}) == ":1\r\n");
			std::this_thread::sleep_for(50ms);
		}

		WHEN("the files are replayed after the deadlines") {
			Restarted server(dir.path(), snapshot.path());

			THEN("commands saw the keys as they were") {
				CHECK(server.run({"GET", "k"}) == "$1\r\n6\r\n");
				CHECK(server.run({"GET", "gone"}) == "$1\r\n1\r\n");
			}
		}
	}
}

SCENARIO("A log that ends in the middle of a command", "[aof]") {
	GIVEN("a log with a torn last command") {
		TempDir dir;
		test::TempFile snapshot;
		fs::path incr;
		std::string whole;
		{
			AofServer server(dir.path(), snapshot.path());
			REQUIRE(server.start().has_value());
			server.run({"SET", "a", "1"});
			incr  = server.aof.files().back();
			whole = server.log();
		}
		{
			std::ofstream out(incr, std::ios::binary | std::ios::app);
			out << "*3\r\n$3\r\nSET\r\n$1\r\nb";
		}

		WHEN("the server starts") {
			Restarted server(dir.path(), snapshot.path());

			THEN("the torn command is cut off and the rest is kept") {
				CHECK(server.run({"GET", "a"}) == "$1\r\n1\r\n");
				CHECK(server.run({"EXISTS", "b"}) == ":0\r\n");
				CHECK(fs::file_size(incr) == whole.size());
			}
		}
	}

	GIVEN("a log with a command that cannot be parsed") {
		TempDir dir;
		test::TempFile snapshot;
		fs::path incr;
		{
			AofServer server(dir.path(), snapshot.path());
			REQUIRE(server.start().has_value());
			server.run({"SET", "a", "1"});
			incr = server.aof.files().back();
		}
		{
			std::ofstream out(incr, std::ios::binary | std::ios::app);
			out << "*x\r\n";
		}

		WHEN("the server starts") {
			AofServer server(dir.path(), snapshot.path());

			THEN("it refuses to") { CHECK_FALSE(server.start().has_value()); }
		}
	}
}

SCENARIO("Turning the append-only file on keeps the snapshot", "[aof]") {
	GIVEN("a snapshot written by a server without AOF") {
		TempDir dir;
		test::TempFile snapshot;
		{
			test::Server plain;
			plain.run({"SET", "old", "1"});
			// Saved to the path the AOF server will be given.
			SnapshotManager manager(snapshot.path());
			REQUIRE(manager.save(plain.db).has_value());
		}

		WHEN("a server starts with AOF over it") {
			{
				AofServer server(dir.path(), snapshot.path());
				REQUIRE(server.start().has_value());
				CHECK(server.run({"GET", "old"}) == "$1\r\n1\r\n");
				server.run({"SET", "new", "2"});
			}

			THEN("the snapshot's keys survive a restart without it") {
				fs::remove(snapshot.path());
				Restarted server(dir.path(), snapshot.path());
				CHECK(server.run({"GET", "old"}) == "$1\r\n1\r\n");
				CHECK(server.run({"GET", "new"}) == "$1\r\n2\r\n");
			}
		}
	}
}

SCENARIO("Rewriting the log", "[aof][rewrite][fork]") {
#ifdef _WIN32
	SKIP("BGREWRITEAOF forks, and Windows cannot");
#else
	GIVEN("a server with a long log") {
		TempDir dir;
		test::TempFile snapshot;
		{
			AofServer server(dir.path(), snapshot.path());
			REQUIRE(server.start().has_value());
			for (int i = 0; i < 100; ++i) server.run({"INCR", "counter"});
			server.run({"SET", "keep", "me"});

			WHEN("it is rewritten while commands keep coming") {
				REQUIRE(server.aof.start_rewrite(server.db).has_value());
				server.run({"INCR", "counter"}); // after the fork: not in the base
				for (int i = 0; i < 500 && server.aof.rewrite_in_progress();
					 ++i) {
					std::this_thread::sleep_for(10ms);
					server.aof.poll();
				}
				REQUIRE_FALSE(server.aof.rewrite_in_progress());

				THEN("the files are one base and one short incremental log") {
					const auto files = server.aof.files();
					REQUIRE(files.size() == 2);
					CHECK(files[0].filename().string().contains("base"));
					CHECK(server.log() == logged({"INCR", "counter"}));
				}
			}
		}

		AND_WHEN("a server starts over the rewritten files") {
			Restarted server(dir.path(), snapshot.path());

			THEN("nothing was lost") {
				CHECK(server.run({"GET", "counter"}) == "$3\r\n101\r\n");
				CHECK(server.run({"GET", "keep"}) == "$2\r\nme\r\n");
			}
		}
	}
#endif
}

SCENARIO("A keyspace that tracks its expiry", "[aof][database]") {
	GIVEN("a key with a deadline in the past, expiry disabled") {
		Database db;
		db.set("k", String("v"));
		db.set_expiry_enabled(false);
		db.track_expired(true);
		db.set_expiry("k", DeadlineTimer::after(-1s));

		THEN("the key is kept") {
			CHECK(db.contains("k"));
			CHECK(db.active_expire_cycle(10) == 0);
			CHECK_FALSE(db.has_expired());
		}

		WHEN("expiry is enabled") {
			db.set_expiry_enabled(true);

			THEN("the key goes, and is remembered") {
				CHECK_FALSE(db.contains("k"));
				std::vector<std::string> expired;
				db.drain_expired(
					[&](std::string_view key) { expired.emplace_back(key); });
				CHECK(expired == std::vector<std::string>{"k"});
				CHECK_FALSE(db.has_expired());
			}
		}
	}
}
