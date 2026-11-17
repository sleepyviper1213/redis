// Key expiry across a restart. These are two phases of one scenario, run by
// scripts/test_expiry_server.sh with a server restart between them, so both are
// hidden from the normal run ([.]). Scenario IDs are those of
// docs/features/key_expiry/test_plan.md; issue #43.
//
//   [restart-write]   TC-122: keys expire by the sweep (the script then checks
//                     the trace log); TC-117: keys with deadlines are saved
//   [restart-verify]  TC-117, TC-118: after the restart, a key kept its
//                     deadline and a key whose deadline passed while the
//                     server was down is not restored
#include "test_client.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {
// The year 2100: still ahead after any restart in a test.
constexpr const char *FAR_DEADLINE_MS = "4102444800000";
// Passes while the server is down: the script waits longer than this.
constexpr const char *SHORT_DEADLINE_MS = "1500";

int64_t integer(TestClient &client, const std::string &command) {
	const auto reply = client.send(command.c_str());
	return reply.ok ? std::stoll(reply.value) : -1;
}
} // namespace

TEST_CASE("TC-117, TC-122: write keys with deadlines and let some expire",
		  "[restart-write][TC-117][TC-122][.]") {
	TestClient client;
	REQUIRE(client.is_connected());
	REQUIRE(client.send("FLUSHDB").ok);

	// TC-122: these expire by the sweep, nobody reads them. Their names are
	// distinctive so the script can check that the trace log never repeats them.
	for (int i = 0; i < 20; ++i) {
		const auto key = fmt::format("secret_expiring_{}", i);
		REQUIRE(client.send("SET %s v PX 50", key.c_str()).ok);
	}
	// DBSIZE does not look keys up, so only the sweep brings it down.
	for (int i = 0; i < 150 && integer(client, "DBSIZE") != 0; ++i)
		std::this_thread::sleep_for(20ms);
	REQUIRE(integer(client, "DBSIZE") == 0);

	// TC-117: one key that stays, one that is gone by the time of the restart.
	REQUIRE(client.send("SET live v PXAT %s", FAR_DEADLINE_MS).ok);
	REQUIRE(client.send("SET short v PX %s", SHORT_DEADLINE_MS).ok);
	REQUIRE(client.send("SET plain v").ok);
	REQUIRE(client.send("SAVE").value == "OK");
}

TEST_CASE("TC-117, TC-118: deadlines after a restart", "[restart-verify][TC-117][TC-118][.]") {
	TestClient client;
	REQUIRE(client.is_connected());

	SECTION("a key whose deadline passed while the server was down is gone") {
		CHECK(client.send("EXISTS short").value == "0");
		CHECK(client.send("TTL short").value == "-2");
		CHECK(integer(client, "DBSIZE") == 2);
	}

	SECTION("a key keeps its absolute deadline, a plain key has none") {
		CHECK(client.send("GET live").value == "v");
		// About 2.3 trillion ms ahead; far more than a restart could cost.
		CHECK(integer(client, "PTTL live") > 1'000'000'000'000);
		CHECK(client.send("TTL plain").value == "-1");
	}

	SECTION("keys skipped on load are not counted as expired") {
		const auto stats = client.send("INFO stats").value;
		CHECK(stats.find("expired_keys:0\r\n") != std::string::npos);
	}
}
