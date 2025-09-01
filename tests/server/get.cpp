#include "server_listener.hpp"
#include "test_client.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Equals;

SCENARIO("TC-015: Retrieve existing key", "[get]") {
	GIVEN("A connected client and an existing key") {
		TestClient client;
		REQUIRE(client.is_connected());

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

SCENARIO("TC-016: Retrieve non-existing key", "[get]") {
	GIVEN("A connected client") {
		TestClient client;
		REQUIRE(client.is_connected());

		WHEN("Sending GET command for non-existing key") {
			auto res = client.send("GET %s", "key");

			THEN("The response is nil") {
				REQUIRE(res.ok);
				REQUIRE_THAT(res.value, Equals("(nil)"));
			}
		}
	}
}

SCENARIO("TC-017: Retrieve after overwrite", "[get]") {
	GIVEN("A connected client and an overwritten key") {
		TestClient client;
		REQUIRE(client.is_connected());

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

SCENARIO("TC-018: Retrieve key with empty value", "[get]") {
	GIVEN("A connected client and a key with empty value") {
		TestClient client;
		REQUIRE(client.is_connected());

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

SCENARIO("TC-019: Retrieve key with binary value", "[get]") {
	GIVEN("A connected client and a key with binary value") {
		TestClient client;
		REQUIRE(client.is_connected());

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

SCENARIO("TC-020: Retrieve with missing arguments", "[get]") {
	GIVEN("A connected client") {
		TestClient client;
		REQUIRE(client.is_connected());

		WHEN("Sending GET command without key") {
			auto res = client.send("GET");

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error,
							 Equals("ERR wrong number of arguments"));
			}
		}
	}
}

SCENARIO("TC-021: Retrieve with invalid key", "[get]") {
	GIVEN("A connected client") {
		TestClient client;
		REQUIRE(client.is_connected());

		WHEN("Sending GET command with empty key") {
			auto res = client.send("GET %s", "");

			THEN("The command fails with error") {
				REQUIRE(!res.ok);
				REQUIRE_THAT(res.error, Equals("ERR invalid key"));
			}
		}
	}
}
