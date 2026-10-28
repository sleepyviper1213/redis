#include "redis.fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Equals;

SCENARIO_METHOD(RedisTestFixture, "TC-015: Retrieve existing key", "[get]") {
	GIVEN("A connected client and an existing key") {

		auto setup_res = client.send("SET %s %s", "name", "Alice");
		REQUIRE(setup_res.ok);

		WHEN("Sending GET command for the key") {
			auto res = client.send("GET %s", "name");

			THEN("The value is retrieved correctly") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("Alice"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-016: Retrieve non-existing key", "[get]") {
	GIVEN("A connected client") {

		WHEN("Sending GET command for non-existing key") {
			auto res = client.send("GET %s", "key");

			THEN("The response is nil") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("(nil)"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-017: Retrieve after overwrite", "[get]") {
	GIVEN("A connected client and an overwritten key") {

		client.send("SET %s %s", "name", "Alice");
		auto setup_res = client.send("SET %s %s", "name", "Bob");
		REQUIRE(setup_res.ok);

		WHEN("Sending GET command for the key") {
			auto res = client.send("GET %s", "name");

			THEN("The updated value is retrieved") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("Bob"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-018: Retrieve key with empty value", "[get]") {
	GIVEN("A connected client and a key with empty value") {

		auto setup_res = client.send("SET %s %s", "key", "");
		REQUIRE(setup_res.ok);

		WHEN("Sending GET command for the key") {
			auto res = client.send("GET %s", "key");

			THEN("The empty value is retrieved") {
				REQUIRE(res.ok);
				REQUIRE(res.value.empty());
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-019: Retrieve key with binary value", "[get]") {
	GIVEN("A connected client and a key with binary value") {

		const char binary_data[] = "\x00\x01";
		auto setup_res =
			client.send("SET %s %b", "bin", binary_data, (size_t)2);
		REQUIRE(setup_res.ok);

		WHEN("Sending GET command for the key") {
			auto res = client.send("GET %s", "bin");

			THEN("The binary value is retrieved correctly") {
				REQUIRE(res.ok);
				REQUIRE(res.value == std::string(binary_data, 2));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture, "TC-020: Retrieve with missing arguments", "[get]") {
	GIVEN("A connected client") {

		WHEN("Sending GET command without key") {
			auto res = client.send("GET");

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error,
							 Equals("ERR wrong number of arguments for 'get' command"));
			}
		}
	}
}

// Redis keys are binary-safe; an empty key is valid and simply missing here.
SCENARIO_METHOD(RedisTestFixture, "TC-021: Retrieve with empty key", "[get]") {
	GIVEN("A connected client") {
		WHEN("Sending GET command with empty key") {
			auto res = client.send("GET %s", "");

			THEN("The reply is nil") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("(nil)"));
			}
		}
	}
}
