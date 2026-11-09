#include "commands.fixture.hpp"
#include "core/logging.hpp"
#include "server/log_channels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace redis;

SCENARIO("Changing the log level at runtime", "[commands][config]") {
	GIVEN("a server logging at info") {
		test::Server server;
		REQUIRE(logging::set_level("info"));

		WHEN("CONFIG SET loglevel names a level") {
			const auto reply = server.run({"CONFIG", "SET", "loglevel", "debug"});

			THEN("it applies to the default logger and every channel") {
				CHECK(reply == "+OK\r\n");
				CHECK(logging::level() == "debug");
				for (const auto which : logging::ALL_CHANNELS)
					CHECK(logging::logger_for(which).level() ==
						  spdlog::level::debug);
			}

			AND_THEN("CONFIG GET reports it") {
				CHECK(server.run({"CONFIG", "GET", "loglevel"}) ==
					  "*2\r\n$8\r\nloglevel\r\n$5\r\ndebug\r\n");
			}
		}

		WHEN("the level is not one") {
			const auto reply = server.run({"CONFIG", "SET", "loglevel", "loud"});

			THEN("it is refused and nothing changes") {
				CHECK(reply.starts_with("-ERR Invalid argument 'loud'"));
				CHECK(logging::level() == "info");
			}
		}

		WHEN("another parameter is set") {
			const auto reply = server.run({"CONFIG", "SET", "port", "1"});

			THEN("it is refused") {
				CHECK(reply.starts_with("-ERR Unsupported CONFIG parameter"));
			}
		}

		// Leave the process as the other tests expect it.
		REQUIRE(logging::set_level("info"));
	}
}

SCENARIO("CONFIG SET is off unless the operator enables it",
		 "[commands][config][security]") {
	GIVEN("a server started without --enable-config-set") {
		test::Server server;
		server.ctx.config_set_enabled = false;
		REQUIRE(logging::set_level("info"));

		WHEN("a client tries to change the log level") {
			const auto reply = server.run({"CONFIG", "SET", "loglevel", "trace"});

			THEN("it is refused and nothing changes") {
				CHECK(reply.starts_with("-ERR CONFIG SET is disabled"));
				CHECK(logging::level() == "info");
			}
		}
		WHEN("a client reads settings") {
			THEN("CONFIG GET still works") {
				CHECK(server.run({"CONFIG", "GET", "loglevel"}) ==
					  "*2\r\n$8\r\nloglevel\r\n$4\r\ninfo\r\n");
			}
		}
	}
}
