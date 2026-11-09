#include "commands.fixture.hpp"
#include "server/net/guard.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace redis;

namespace {
// The bulk string's payload, without its "$<n>\r\n" header and trailing CRLF.
std::string payload(const std::string &reply) {
	const auto start = reply.find("\r\n") + 2;
	return reply.substr(start, reply.size() - start - 2);
}
} // namespace

SCENARIO("INFO reports the server's state", "[commands][info]") {
	GIVEN("a server that has run some commands") {
		test::Server server;
		server.run({"SET", "a", "1"});
		server.run({"SET", "b", "2"});
		server.run({"EXPIRE", "b", "100"});
		server.run({"NOSUCHCOMMAND"});

		WHEN("INFO is called without a section") {
			const auto text = payload(server.run({"INFO"}));

			THEN("every section is present") {
				for (const char *title : {"# Server", "# Clients", "# Stats",
										  "# Persistence", "# Keyspace"})
					CHECK(text.find(title) != std::string::npos);
			}
			AND_THEN("it counts the commands that ran, not the unknown one") {
				// SET, SET, EXPIRE; INFO itself is counted after it replies.
				CHECK(text.find("total_commands_processed:3\r\n") !=
					  std::string::npos);
			}
			AND_THEN("the keyspace line has the key and expiry counts") {
				CHECK(text.find("db0:keys=2,expires=1\r\n") != std::string::npos);
			}
		}

		WHEN("INFO names one section") {
			const auto text = payload(server.run({"INFO", "keyspace"}));

			THEN("only that section is returned") {
				CHECK(text.starts_with("# Keyspace\r\n"));
				CHECK(text.find("# Stats") == std::string::npos);
			}
		}

		WHEN("INFO names a section that does not exist") {
			THEN("the reply is empty") {
				CHECK(server.run({"INFO", "nope"}) == "$0\r\n\r\n");
			}
		}
	}
}

SCENARIO("INFO counts lookups and times each command", "[commands][info]") {
	GIVEN("a server with one key") {
		test::Server server;
		server.run({"SET", "a", "1"});
		const auto hits   = server.db.keyspace_hits();
		const auto misses = server.db.keyspace_misses();

		WHEN("one existing and one missing key are read") {
			server.run({"GET", "a"});
			server.run({"GET", "nokey"});
			server.run({"GET", "a"});

			THEN("the hits and misses are counted") {
				CHECK(server.db.keyspace_hits() == hits + 2);
				CHECK(server.db.keyspace_misses() == misses + 1);
				const auto text = payload(server.run({"INFO", "stats"}));
				CHECK(text.find("keyspace_hits:" +
								std::to_string(hits + 2) + "\r\n") !=
					  std::string::npos);
				CHECK(text.find("expired_keys:0\r\n") != std::string::npos);
			}
		}

		WHEN("INFO is asked for commandstats") {
			server.run({"GET", "a"});
			server.run({"GET", "a"});

			THEN("each command has its calls and time") {
				const auto text = payload(server.run({"INFO", "commandstats"}));
				CHECK(text.starts_with("# Commandstats\r\n"));
				CHECK(text.find("cmdstat_get:calls=2,usec=") != std::string::npos);
				CHECK(text.find("cmdstat_set:calls=1,usec=") != std::string::npos);
			}
			AND_THEN("the default sections leave it out") {
				CHECK(payload(server.run({"INFO"})).find("# Commandstats") ==
					  std::string::npos);
				CHECK(payload(server.run({"INFO", "all"})).find("# Commandstats") !=
					  std::string::npos);
			}
		}
	}
}

SCENARIO("INFO reports the memory the keyspace holds", "[commands][info]") {
	GIVEN("a server") {
		test::Server server;
		// The tables keep their buckets after a delete, so let them grow first.
		server.run({"SET", "warm", "up"});
		server.run({"DEL", "warm"});

		WHEN("a large value is stored and then deleted") {
			const auto before = DefaultAllocator::bytes_in_use();
			server.run({"SET", "big", std::string(100'000, 'x')});
			const auto during = DefaultAllocator::bytes_in_use();
			server.run({"DEL", "big"});
			const auto after = DefaultAllocator::bytes_in_use();

			THEN("used_memory rises by at least the value, and falls back") {
				CHECK(during >= before + 100'000);
				CHECK(after == before);
			}
			AND_THEN("INFO memory reports the figure") {
				const auto text = payload(server.run({"INFO", "memory"}));
				CHECK(text.starts_with("# Memory\r\nused_memory:" +
									   std::to_string(after) + "\r\n"));
				CHECK(text.find("used_memory_human:") != std::string::npos);
			}
		}
	}
}

SCENARIO("INFO reports connections when there is a network", "[commands][info]") {
	GIVEN("a server whose guard admits one client and refuses another") {
		test::Server server;
		NetGuard guard(NetLimits{.max_clients = 1});
		server.ctx.guard = &guard;
		const auto ticket = guard.admit();
		REQUIRE(ticket.has_value());
		REQUIRE(!guard.admit().has_value());
		guard.record(Violation::TooManyClients, "peer", 2);

		WHEN("INFO is called") {
			const auto text = payload(server.run({"INFO", "clients", "stats"}));

			THEN("clients and connection totals are reported") {
				CHECK(text.find("connected_clients:1\r\n") != std::string::npos);
				CHECK(text.find("maxclients:1\r\n") != std::string::npos);
				CHECK(text.find("rejected_connections:1\r\n") !=
					  std::string::npos);
				CHECK(text.find("total_connections_received:2\r\n") !=
					  std::string::npos);
			}
		}
	}
}
