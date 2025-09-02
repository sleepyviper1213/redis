#include "redis_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Equals;

// ---------------------------------------------------------
// Ping/Pong basic cases
// ---------------------------------------------------------

SCENARIO_METHOD(RedisTestFixture,
				"[UT-1001] Single client can PING and receive PONG",
				"[server][ping_pong]") {
	GIVEN("a connected client") {
		WHEN("the client sends PING") {
			auto r = client.send("PING");

			THEN("the server responds with PONG") {
				REQUIRE(r.ok);
				REQUIRE_THAT(r.value, Equals("PONG"));
			}
		}
	}
}

SCENARIO("[UT-1002] Multiple clients can independently PING and receive PONG",
		 "[server][ping_pong]") {
	GIVEN("two connected clients") {
		TestClient c1;
		TestClient c2;

		REQUIRE(c1.is_connected());
		REQUIRE(c2.is_connected());

		WHEN("the first client sends PING") {
			auto r1 = c1.send("PING");

			THEN("the server responds with PONG") {
				REQUIRE(r1.ok);
				REQUIRE_THAT(r1.value, Equals("PONG"));
			}
		}

		WHEN("the second client sends PING") {
			auto r2 = c2.send("PING");

			THEN("the server responds with PONG") {
				REQUIRE(r2.ok);
				REQUIRE_THAT(r2.value, Equals("PONG"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,
				"[UT-1003] PING with custom message returns same message",
				"[server][ping_pong]") {
	GIVEN("a connected client") {
		WHEN("the client sends PING with a custom payload") {
			auto r = client.send("PING HelloWorld");

			THEN("the server echoes back the payload") {
				REQUIRE(r.ok);
				REQUIRE_THAT(r.value, Equals("HelloWorld"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,
				"[UT-1004] Multiple PING requests preserve order",
				"[server][ping_pong]") {
	GIVEN("a connected client") {
		WHEN("the client sends PING twice") {
			auto r1 = client.send("PING One");
			auto r2 = client.send("PING Two");

			THEN("responses arrive in order and match payloads") {
				REQUIRE(r1.ok);
				REQUIRE_THAT(r1.value, Equals("One"));
				REQUIRE(r2.ok);
				REQUIRE_THAT(r2.value, Equals("Two"));
			}
		}
	}
}
