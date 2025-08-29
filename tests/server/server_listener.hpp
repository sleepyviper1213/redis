#pragma once
#include "wait_for_port.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_reporter.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <unistd.h>

class RedisServerListener : public Catch::EventListenerBase {
public:
	using Catch::EventListenerBase::EventListenerBase;

	void testRunStarting(const Catch::TestRunInfo &) override {
		// Kill any stray server (optional, but avoids port conflict)
		std::system("pkill -f redis_server >/dev/null 2>&1 || true");

		// Start server
		const std::string cmd =
#if REDIS_SERVER_LOGS
			std::string(REDIS_SERVER_PATH) + " &";
#else
			std::string(REDIS_SERVER_PATH) + " >redis_test.log 2>&1 &";
#endif
		REQUIRE(std::system(cmd.c_str()) == 0);

		// Give it time to bind
		using namespace std::chrono_literals;
		// Poll until port is open instead of fixed sleep
		REQUIRE(wait_for_port("127.0.0.1", 6379, 3s)); // waits up to 3s
	}

	void testRunEnded(const Catch::TestRunStats &) override {
		// Kill server
		std::system("pkill -f redis_server");
	}
};

CATCH_REGISTER_LISTENER(RedisServerListener);
