// Key expiry through the commands. Scenario IDs (TC-nnn, LI-nn, #n) are those
// of docs/features/key_expiry/test_plan.md; the issue is #43.
#include "absl/strings/match.h"
#include "commands.fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <chrono>
#include <string>
#include <thread>

using namespace redis;
using namespace std::chrono_literals;

namespace {
// A margin that survives a loaded machine (sanitizers, parallel tests): the
// deadline is short, the wait is three times longer.
constexpr auto SHORT_DEADLINE_MS = "20";
constexpr auto PAST_DEADLINE     = 60ms;

/// Give \p key a deadline that passes before this returns.
void make_due(test::Server &server, const std::string &key) {
	REQUIRE(server.run({"PEXPIRE", key, SHORT_DEADLINE_MS}) == ":1\r\n");
	std::this_thread::sleep_for(PAST_DEADLINE);
}

bool contains(const std::string &text, const char *part) {
	return absl::StrContains(text, part);
}
} // namespace

SCENARIO("Every value type takes a deadline",
		 "[commands][expiry][TC-090][TC-092]") {
	GIVEN("a string, a list, a hash and a set") {
		test::Server server;
		server.run({"SET", "str", "v"});
		server.run({"RPUSH", "list", "x"});
		server.run({"HSET", "hash", "f", "v"});
		server.run({"SADD", "set", "m"});

		WHEN("each gets a deadline") {
			THEN("EXPIRE replies 1 and the key keeps its type") {
				for (const char *key : {"str", "list", "hash", "set"})
					CHECK(server.run({"EXPIRE", key, "100"}) == ":1\r\n");
				CHECK(server.run({"TYPE", "str"}) == "+string\r\n");
				CHECK(server.run({"TYPE", "list"}) == "+list\r\n");
				CHECK(server.run({"TYPE", "hash"}) == "+hash\r\n");
				CHECK(server.run({"TYPE", "set"}) == "+set\r\n");
				CHECK(server.run({"TTL", "list"}) == ":100\r\n");
			}
		}

		WHEN("a key has a deadline and gets another") {
			server.run({"EXPIRE", "str", "100"});
			server.run({"EXPIRE", "str", "500"});

			THEN("the new one replaces the old") {
				CHECK(server.run({"TTL", "str"}) == ":500\r\n");
			}
		}
	}
}

SCENARIO("Deadlines already past, invalid amounts and wrong arity",
		 "[commands][expiry][TC-098][TC-099][TC-100][TC-103]") {
	GIVEN("a key") {
		test::Server server;
		server.run({"SET", "k", "v"});

		WHEN("the amount is zero or negative") {
			THEN("the key is deleted at once and the reply is 1") {
				for (const char *cmd : {"EXPIRE", "PEXPIRE"}) {
					for (const char *amount : {"0", "-5"}) {
						server.run({"SET", "k", "v"});
						CHECK(server.run({cmd, "k", amount}) == ":1\r\n");
						CHECK(server.run({"EXISTS", "k"}) == ":0\r\n");
					}
				}
			}
		}

		WHEN("the absolute time is in the past") {
			THEN("the key is deleted at once and the reply is 1") {
				for (const char *cmd : {"EXPIREAT", "PEXPIREAT"}) {
					server.run({"SET", "k", "v"});
					CHECK(server.run({cmd, "k", "1"}) == ":1\r\n");
					CHECK(server.run({"EXISTS", "k"}) == ":0\r\n");
				}
			}
		}

		WHEN("the amount is not an integer") {
			THEN("it is rejected and the key is unchanged") {
				for (const char *cmd :
					 {"EXPIRE", "PEXPIRE", "EXPIREAT", "PEXPIREAT"}) {
					for (const char *amount : {"abc", "1.5", ""})
						CHECK(
							server.run({cmd, "k", amount}) ==
							"-ERR value is not an integer or out of range\r\n");
				}
				CHECK(server.run({"TTL", "k"}) == ":-1\r\n");
				CHECK(server.run({"GET", "k"}) == "$1\r\nv\r\n");
			}
		}

		WHEN("the number of arguments is wrong") {
			THEN("the error names the command in lower case") {
				CHECK(
					server.run({"EXPIRE", "k"}) ==
					"-ERR wrong number of arguments for 'expire' command\r\n");
				CHECK(
					server.run({"EXPIRE", "k", "1", "2"}) ==
					"-ERR wrong number of arguments for 'expire' command\r\n");
				CHECK(server.run({"TTL"}) ==
					  "-ERR wrong number of arguments for 'ttl' command\r\n");
				CHECK(
					server.run({"PERSIST", "k", "x"}) ==
					"-ERR wrong number of arguments for 'persist' command\r\n");
			}
		}
	}
}

SCENARIO("Writes keep or drop a deadline",
		 "[commands][expiry][TC-104c][TC-104d][TC-104e]") {
	GIVEN("keys of each type, each with a deadline") {
		test::Server server;
		server.run({"SET", "n", "1"});
		server.run({"RPUSH", "list", "x"});
		server.run({"HSET", "hash", "f", "v"});
		server.run({"SADD", "set", "m"});
		for (const char *key : {"n", "list", "hash", "set"})
			server.run({"EXPIRE", key, "100"});

		WHEN("they are changed in place") {
			server.run({"INCR", "n"});
			server.run({"APPEND", "n", "0"});
			server.run({"LPUSH", "list", "y"});
			server.run({"HSET", "hash", "g", "w"});
			server.run({"SADD", "set", "o"});

			THEN("each keeps its deadline") {
				for (const char *key : {"n", "list", "hash", "set"})
					CHECK(server.run({"TTL", key}) == ":100\r\n");
			}
		}

		WHEN("a string is replaced by SET or MSET") {
			server.run({"SET", "n", "2"});

			THEN("SET clears the deadline") {
				CHECK(server.run({"TTL", "n"}) == ":-1\r\n");
			}

			AND_WHEN("MSET replaces a key that has one") {
				server.run({"EXPIRE", "n", "100"});
				REQUIRE(server.run({"MSET", "n", "3", "m", "4"}) == "+OK\r\n");

				THEN("MSET clears it") {
					CHECK(server.run({"TTL", "n"}) == ":-1\r\n");
				}
			}
		}

		WHEN("a key is deleted and created again") {
			server.run({"DEL", "n"});
			server.run({"SET", "n", "1"});

			THEN("the new key has no deadline") {
				CHECK(server.run({"TTL", "n"}) == ":-1\r\n");
			}
		}

		WHEN("the keyspace is flushed and a key is created again") {
			server.run({"FLUSHDB"});
			server.run({"SET", "list", "v"});

			THEN("the new key has no deadline") {
				CHECK(server.run({"TTL", "list"}) == ":-1\r\n");
			}
		}
	}
}

SCENARIO("A key past its deadline is missing to every command",
		 "[commands][expiry][TC-105][TC-106][TC-106a][TC-106b][TC-106c]") {
	GIVEN("keys of each type whose deadlines have passed, and one live key") {
		test::Server server;
		server.run({"SET", "str", "v"});
		server.run({"RPUSH", "list", "x"});
		server.run({"HSET", "hash", "f", "v"});
		server.run({"SET", "live", "v"});
		for (const char *key : {"str", "list", "hash"})
			REQUIRE(server.run({"PEXPIRE", key, SHORT_DEADLINE_MS}) ==
					":1\r\n");
		std::this_thread::sleep_for(PAST_DEADLINE);

		THEN("reads see them as missing") {
			CHECK(server.run({"GET", "str"}) == "$-1\r\n");
			CHECK(server.run({"LLEN", "list"}) == ":0\r\n");
			CHECK(server.run({"HGETALL", "hash"}) == "*0\r\n");
		}

		THEN("EXISTS, TYPE, TTL and PTTL see them as missing") {
			CHECK(server.run({"EXISTS", "str", "list", "hash"}) == ":0\r\n");
			CHECK(server.run({"TYPE", "str"}) == "+none\r\n");
			CHECK(server.run({"TTL", "list"}) == ":-2\r\n");
			CHECK(server.run({"PTTL", "hash"}) == ":-2\r\n");
		}

		THEN("KEYS does not list them") {
			CHECK(server.run({"KEYS", "*"}) == "*1\r\n$4\r\nlive\r\n");
		}

		THEN("a write starts from an empty key, of any type") {
			CHECK(server.run({"LPUSH", "str", "x"}) == ":1\r\n");
			CHECK(server.run({"LRANGE", "str", "0", "-1"}) ==
				  "*1\r\n$1\r\nx\r\n");
			CHECK(server.run({"TTL", "str"}) == ":-1\r\n");

			CHECK(server.run({"RPUSH", "list", "y"}) == ":1\r\n");
			CHECK(server.run({"LRANGE", "list", "0", "-1"}) ==
				  "*1\r\n$1\r\ny\r\n");
		}

		THEN("a lookup counts the key in expired_keys, once") {
			const auto before = server.db.expired_keys();
			server.run({"GET", "str"});
			server.run({"GET", "str"});
			CHECK(server.db.expired_keys() == before + 1);
		}
	}
}

SCENARIO("INFO reports the keys with a deadline and the keys that expired",
		 "[commands][expiry][info][TC-120][TC-121]") {
	GIVEN("three keys, two with a deadline") {
		test::Server server;
		server.run({"SET", "a", "1"});
		server.run({"SET", "b", "2"});
		server.run({"SET", "c", "3"});
		server.run({"EXPIRE", "a", "100"});
		server.run({"EXPIRE", "b", "100"});

		THEN("INFO keyspace counts both") {
			CHECK(contains(server.run({"INFO", "keyspace"}),
						   "db0:keys=3,expires=2\r\n"));
		}

		WHEN("one deadline is removed with PERSIST") {
			server.run({"PERSIST", "a"});

			THEN("expires drops by one") {
				CHECK(contains(server.run({"INFO", "keyspace"}),
							   "db0:keys=3,expires=1\r\n"));
			}
		}
	}

	GIVEN("five keys that expire, by a read and by the sweep") {
		test::Server server;
		for (int i = 0; i < 5; ++i) {
			const auto key = fmt::format("k{}", i);
			server.run({"SET", key, "v"});
			REQUIRE(server.run({"PEXPIRE", key, SHORT_DEADLINE_MS}) ==
					":1\r\n");
		}
		std::this_thread::sleep_for(PAST_DEADLINE);
		server.run({"GET", "k0"});
		server.db.active_expire_cycle(100);

		THEN("expired_keys is 5 and FLUSHDB adds nothing") {
			CHECK(
				contains(server.run({"INFO", "stats"}), "expired_keys:5\r\n"));
			server.run({"SET", "x", "v"});
			server.run({"FLUSHDB"});
			CHECK(
				contains(server.run({"INFO", "stats"}), "expired_keys:5\r\n"));
		}
	}
}

// Bug #40: the expectation is the correct one, and the test is expected to fail
// until the bug is fixed. When it starts to "fail" because it passes, the bug
// is fixed: remove [!shouldfail].
SCENARIO("DEL of a key past its deadline replies 0, as Redis does (#40)",
		 "[commands][expiry][!shouldfail]") {
	GIVEN("a key whose deadline has passed but which nothing has removed") {
		test::Server server;
		server.run({"SET", "k", "v"});
		make_due(server, "k");

		THEN("DEL finds no key, and the key counts as expired") {
			const auto before = server.db.expired_keys();
			CHECK(server.run({"DEL", "k"}) == ":0\r\n");
			CHECK(server.db.expired_keys() == before + 1);
		}
	}
}

// By design (LI-05), and not implemented (#41, #42). Pinned so that changing
// one is deliberate: replace the test with a normal scenario when it changes.
SCENARIO("Key expiry limits by design and commands not implemented",
		 "[commands][expiry][limit]") {
	GIVEN("a key whose deadline has passed but which nothing has removed") {
		test::Server server;
		server.run({"SET", "k", "v"});
		make_due(server, "k");

		THEN("DBSIZE still counts it, until it is removed (LI-05)") {
			CHECK(server.run({"DBSIZE"}) == ":1\r\n");
			server.db.active_expire_cycle(100);
			CHECK(server.run({"DBSIZE"}) == ":0\r\n");
		}
	}

	GIVEN("a key") {
		test::Server server;
		server.run({"SET", "k", "v"});

		THEN("EXPIRE takes no NX, XX, GT or LT option (#41)") {
			for (const char *option : {"NX", "XX", "GT", "LT"})
				CHECK(
					server.run({"EXPIRE", "k", "10", option}) ==
					"-ERR wrong number of arguments for 'expire' command\r\n");
		}

		THEN("EXPIRETIME and PEXPIRETIME are unknown commands (#42)") {
			CHECK(server.run({"EXPIRETIME", "k"}) ==
				  "-ERR unknown command 'EXPIRETIME', with args beginning "
				  "with: 'k' \r\n");
			CHECK(server.run({"PEXPIRETIME", "k"}) ==
				  "-ERR unknown command 'PEXPIRETIME', with args beginning "
				  "with: 'k' \r\n");
		}
	}
}

// TC-087, TC-088, TC-089: deadlines in the future, set by each command.
SCENARIO("Deadlines in the future are read back in their unit",
		 "[commands][expiry][TC-087][TC-088][TC-089]") {
	GIVEN("a key, and PEXPIRE with 5000 milliseconds") {
		test::Server server;
		server.run({"SET", "k", "v"});
		REQUIRE(server.run({"PEXPIRE", "k", "5000"}) == ":1\r\n");

		THEN("PTTL is in milliseconds, 4000 to 5000") {
			const auto ms = std::stoll(server.run({"PTTL", "k"}).substr(1));
			CHECK(ms > 4000);
			CHECK(ms <= 5000);
		}
	}

	GIVEN("a key, and EXPIREAT 100 seconds from now") {
		test::Server server;
		server.run({"SET", "k", "v"});
		const auto now = std::chrono::duration_cast<std::chrono::seconds>(
							 std::chrono::system_clock::now().time_since_epoch())
							 .count();
		REQUIRE(server.run({"EXPIREAT", "k", fmt::format("{}", now + 100)}) ==
				":1\r\n");

		THEN("TTL is about 100 seconds") {
			const auto secs = std::stoll(server.run({"TTL", "k"}).substr(1));
			CHECK(secs >= 98);
			CHECK(secs <= 100);
		}
	}

	GIVEN("a key, and PEXPIREAT 100000 milliseconds from now") {
		test::Server server;
		server.run({"SET", "k", "v"});
		const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::system_clock::now().time_since_epoch())
							 .count();
		REQUIRE(server.run({"PEXPIREAT", "k", fmt::format("{}", now + 100000)}) ==
				":1\r\n");

		THEN("PTTL is about 100000 milliseconds") {
			const auto ms = std::stoll(server.run({"PTTL", "k"}).substr(1));
			CHECK(ms > 98000);
			CHECK(ms <= 100000);
		}
	}
}
