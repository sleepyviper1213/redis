#include "redis_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace Catch::Matchers;

SCENARIO_METHOD(RedisTestFixture,"TC-007: Save key with empty string value", "[set]") {
	GIVEN("A connected client") {
		WHEN("Sending SET command with empty value") {
			auto res = client.send("SET %s %s", "key", "");

			THEN("The command succeeds") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("OK"));
			}

			AND_WHEN("Retrieving the value with GET") {
				auto get_res = client.send("GET %s", "key");

				THEN("The value is empty string") {
					REQUIRE(get_res.ok);
					REQUIRE(get_res.value.empty());
				}
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,"TC-008: Save a valid key-value pair", "[set]") {
	GIVEN("A connected client") {
		WHEN("Sending SET command with valid key-value") {
			auto res = client.send("SET %s %s", "name", "John");

			THEN("The command succeeds") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("OK"));
			}

			AND_WHEN("Retrieving the value with GET") {
				auto get_res = client.send("GET %s", "name");

				THEN("The value is retrieved correctly") {
					REQUIRE(get_res.ok);
					REQUIRE_THAT(get_res.value, Equals("John"));
				}
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,"TC-009: Save empty key (invalid)", "[set]") {
	GIVEN("A connected client") {
		WHEN("Sending SET command with empty key") {
			auto res = client.send("SET %s %s", "", "John");

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error, Equals("ERR invalid key"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,"TC-010: Save an existing key with a new value", "[set]") {
	GIVEN("A connected client and an existing key") {
		auto setup_res = client.send("SET %s %s", "name", "John");
		REQUIRE(setup_res.ok);

		WHEN("Sending SET command to update the value") {
			auto res = client.send("SET %s %s", "name", "Jane");

			THEN("The command succeeds") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("OK"));
			}

			AND_WHEN("Retrieving the updated value with GET") {
				auto get_res = client.send("GET %s", "name");

				THEN("The value is updated correctly") {
					REQUIRE(get_res.ok);
					REQUIRE_THAT(get_res.value, Equals("Jane"));
				}
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,"TC-011: Save with missing arguments", "[set]") {
	GIVEN("A connected client") {
		WHEN("Sending SET command without value") {
			auto res = client.send("SET %s", "name");

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error,
							 Equals("ERR wrong number of arguments"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,"TC-012: Save large value (>1MB)", "[set]") {
	GIVEN("A connected client") {
		std::string large_value(1024 * 1024 + 1, 'a');

		WHEN("Sending SET command with large value") {
			auto res = client.send("SET %s %s", "bigkey", large_value.c_str());

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error, Equals("ERR value too large"));
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,"TC-013: Save key with special characters", "[set]") {
	GIVEN("A connected client") {
		WHEN("Sending SET command with special key") {
			auto res = client.send("SET %s %s", "k@#y", "John");

			THEN("The command succeeds") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("OK"));
			}

			AND_WHEN("Retrieving the value with GET") {
				auto get_res = client.send("GET %s", "k@#y");

				THEN("The value is retrieved correctly") {
					REQUIRE(get_res.ok);
					REQUIRE_THAT(get_res.value, Equals("John"));
				}
			}
		}
	}
}

SCENARIO_METHOD(RedisTestFixture,"TC-014: Save binary-safe value", "[set]") {
	GIVEN("A connected client") {
		const char binary_data[] = "\x00\x01";

		WHEN("Sending SET command with binary value") {
			auto res = client.send("SET %s %b", "bin", binary_data, (size_t)2);

			THEN("The command succeeds") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("OK"));
			}

			AND_WHEN("Retrieving the value with GET") {
				auto get_res = client.send("GET %s", "bin");

				THEN("The binary value is retrieved correctly") {
					REQUIRE(get_res.ok);
					REQUIRE(get_res.value == std::string(binary_data, 2));
				}
			}
		}
	}
}
