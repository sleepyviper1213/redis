#pragma once
#include "test_client.hpp"

#include <catch2/catch_test_macros.hpp>

struct RedisTestFixture {
	TestClient client;

	RedisTestFixture() {
		if (!client.is_connected())
			FAIL("Failed to connect to Redis server: " << client.last_error());
		auto result = client.send("FLUSHDB");
		if (!result.ok) FAIL(result.error);
	}
};
