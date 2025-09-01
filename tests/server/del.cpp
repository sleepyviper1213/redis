#include "redis_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Equals;

SCENARIO_METHOD(RedisTestFixture, "TC-022: Delete existing key", "[del]") {
	GIVEN("A connected client and an existing key") {
		auto setup_res = client.send("SET %s %s", "name", "val");
		REQUIRE(setup_res.ok);

		WHEN("Sending DEL command for the key") {
			auto res = client.send("DEL %s", "name");

			THEN("One key is deleted") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("1"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-023: Delete already deleted key",
				"[del]") {
	GIVEN("A connected client and a deleted key") {
		client.send("SET %s %s", "name", "val");
		auto setup_res = client.send("DEL %s", "name");
		REQUIRE(setup_res.ok);

		WHEN("Sending DEL command again for the key") {
			auto res = client.send("DEL %s", "name");

			THEN("No key is deleted") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("0"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-024: Delete multiple keys", "[del]") {
	GIVEN("A connected client and multiple existing keys") {
		client.send("SET %s %s", "name", "val");
		auto setup_res = client.send("SET %s %s", "age", "val");
		REQUIRE(setup_res.ok);

		WHEN("Sending DEL command for multiple keys") {
			auto res = client.send("DEL %s %s", "name", "age");

			THEN("Two keys are deleted") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("2"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-025: Delete without argument", "[del]") {
	GIVEN("A connected client") {
		WHEN("Sending DEL command without keys") {
			auto res = client.send("DEL");

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error,
							 Equals("ERR wrong number of arguments"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-026: Delete with empty key", "[del]") {
	GIVEN("A connected client") {
		WHEN("Sending DEL command with empty key") {
			auto res = client.send("DEL %s", "");

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error, Equals("ERR invalid key"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-027: Delete key with binary name",
				"[del]") {
	GIVEN("A connected client and a key with binary name") {
		const char *binary_key = "\x00key";
		auto setup_res = client.send("SET %b %s", binary_key, (size_t)5, "val");
		REQUIRE(setup_res.ok);

		WHEN("Sending DEL command for the binary key") {
			auto res = client.send("DEL %b", binary_key, (size_t)5);

			THEN("One key is deleted") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("1"));
			}
		}
	}
}
