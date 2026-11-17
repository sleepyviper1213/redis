// Key expiry against a running server: the real clock and the real sweep.
// Scenario IDs are those of docs/features/key_expiry/test_plan.md; issue #43.
//
// Timing: a key is given a short deadline, then the test polls with a timeout
// instead of sleeping once, so a loaded machine slows the test and does not fail
// it. The sweep runs every 100 ms.
#include "redis.fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {
/// Poll \p done every 20 ms until it is true or \p timeout passes.
template <typename Done>
bool wait_for(Done done, std::chrono::milliseconds timeout = 3000ms) {
	const auto end = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < end) {
		if (done()) return true;
		std::this_thread::sleep_for(20ms);
	}
	return done();
}

/// A number from an INFO section, such as `expired_keys` from `stats`; -1 if
/// it is not there.
int64_t info_number(TestClient &client, const char *section,
					const std::string &field) {
	const auto reply = client.send("INFO %s", section);
	if (!reply.ok) return -1;
	const auto key = field + ":";
	const auto at  = reply.value.find(key);
	if (at == std::string::npos) return -1;
	return std::stoll(reply.value.substr(at + key.size()));
}

int64_t integer(TestClient &client, const std::string &command) {
	const auto reply = client.send(command.c_str());
	return reply.ok ? std::stoll(reply.value) : -1;
}
} // namespace

TEST_CASE_METHOD(RedisTestFixture,
				 "TC-105: A key past its deadline is gone for every command",
				 "[expiry]") {
	REQUIRE(client.send("SET short v").ok);
	REQUIRE(client.send("SET live v").ok);
	REQUIRE(client.send("PEXPIRE short 50").value == "1");

	// Polling with GET removes the key by access, whichever comes first.
	REQUIRE(wait_for([&] { return client.send("GET short").value == "(nil)"; }));

	CHECK(client.send("EXISTS short").value == "0");
	CHECK(client.send("TYPE short").value == "none");
	CHECK(client.send("TTL short").value == "-2");
	CHECK(client.send("GET live").value == "v");
}

TEST_CASE_METHOD(RedisTestFixture,
				 "TC-107: Keys nobody reads are removed by the sweep",
				 "[expiry][TC-107]") {
	constexpr int KEYS = 100;
	for (int i = 0; i < KEYS; ++i) {
		const auto key = fmt::format("sweep:{}", i);
		REQUIRE(client.send("SET %s v", key.c_str()).ok);
		REQUIRE(client.send("PEXPIRE %s 50", key.c_str()).value == "1");
	}
	REQUIRE(client.send("SET persistent v").ok);
	const auto before = info_number(client, "stats", "expired_keys");
	REQUIRE(before >= 0);

	// DBSIZE does not look keys up, so only the sweep can bring it down.
	REQUIRE(wait_for([&] { return integer(client, "DBSIZE") == 1; }));

	CHECK(info_number(client, "stats", "expired_keys") == before + KEYS);
	CHECK(client.send("INFO keyspace").value.find("db0:keys=1,expires=0") !=
		  std::string::npos);
	CHECK(client.send("GET persistent").value == "v");
}
