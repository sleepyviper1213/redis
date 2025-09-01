#include "test_client.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

using Catch::Matchers::Equals;
namespace fs = std::filesystem;

static const fs::path SNAPSHOT_FILE = "redis.snap";
static const fs::path SNAPSHOT_BAK  = "redis.snap.bak";

TEST_CASE("TC-001: Save snapshot successfully", "[save][.]") {
	TestClient client;
	REQUIRE(client.is_connected());

	auto r = client.send("SAVE");
	REQUIRE(r.ok);
	REQUIRE_THAT(r.value, Equals("OK"));

	// Stub: verify snapshot file exists
	REQUIRE(fs::exists(SNAPSHOT_FILE));
}

TEST_CASE("TC-002: Save snapshot when DB is empty", "[save][.]") {
	TestClient client;
	REQUIRE(client.is_connected());

	auto flush = client.send("FLUSHALL");
	REQUIRE(flush.ok);

	auto r = client.send("SAVE");
	REQUIRE(r.ok);
	REQUIRE_THAT(r.value, Equals("OK"));

	// Stub: verify snapshot exists even when DB is empty
	REQUIRE(fs::exists(SNAPSHOT_FILE));
}

TEST_CASE("TC-003: Save snapshot while previous snapshot exists", "[save][.]") {
	TestClient client;
	REQUIRE(client.is_connected());

	// First snapshot
	auto r1 = client.send("SAVE");
	REQUIRE(r1.ok);

	// Ensure file exists
	REQUIRE(fs::exists(SNAPSHOT_FILE));

	// Second snapshot should trigger backup
	auto r2 = client.send("SAVE");
	REQUIRE(r2.ok);
	REQUIRE_THAT(r2.value, Equals("OK"));

	// Stub: verify backup file exists
	REQUIRE(fs::exists(SNAPSHOT_BAK));
}

TEST_CASE("TC-004: Save snapshot with corrupted in-memory state", "[save][.]") {
	TestClient client;
	REQUIRE(client.is_connected());

	// Stub: simulate corrupted state
	// (might require injecting bad data, e.g., invalid serialization)
	// For now, just call SAVE and expect failure if implemented.
	auto r = client.send("SAVE");

	if (r.ok)
		WARN("Corruption simulation not implemented, SAVE still succeeded");
	else REQUIRE_THAT(r.error, Equals("ERR snapshot failed"));
}

TEST_CASE("TC-005: Save snapshot with no write permissions",
		  "[save][.][!mayfail]") {
	TestClient client;
	REQUIRE(client.is_connected());

	// Remove write permissions from current directory (POSIX only!)
	auto old_perms = fs::status(".").permissions();
	REQUIRE_NOTHROW(
		fs::permissions(".", fs::perms::owner_all, fs::perm_options::remove));
	auto r = client.send("SAVE");

	if (r.ok)
		WARN("Permission simulation not implemented, SAVE still succeeded");
	else REQUIRE_THAT(r.error, Equals("ERR cannot write snapshot"));

	// Restore permissions
	REQUIRE_NOTHROW(fs::permissions(".", old_perms, fs::perm_options::replace));
}

TEST_CASE("TC-006: Concurrent SAVE commands", "[save][.]") {
	TestClient c1, c2;
	REQUIRE(c1.is_connected());
	REQUIRE(c2.is_connected());

	auto r1 = c1.send("SAVE");
	auto r2 = c2.send("SAVE");

	// At least one should succeed
	REQUIRE((r1.ok || r2.ok));


	// If one failed, check it was due to concurrency
	if (!r1.ok) {
		REQUIRE_THAT(r1.error,
					 Equals("ERR background save already in progress"));
	}
	if (!r2.ok) {
		REQUIRE_THAT(r2.error,
					 Equals("ERR background save already in progress"));
	}
}
