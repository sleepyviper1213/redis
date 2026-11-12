#include "core/logging.hpp"
#include "server/log_channels.hpp"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <string>
namespace {
/**
 * The level the tests log at: REDIS_TEST_LOGLEVEL when set (trace, debug, ...,
 * as for the server's --log-level), "off" otherwise, so a normal run prints
 * nothing and a debugging run can follow the server's own log lines:
 *
 *     REDIS_TEST_LOGLEVEL=trace redis_unit_test "[session]"
 */
std::string test_log_level() {
	const char *level = std::getenv("REDIS_TEST_LOGLEVEL");
	return level != nullptr && *level != '\0' ? level : "off";
}

// Synchronous logging for the whole run, to the console only: no file, and no
// async worker thread to outlive the test binary's main.
class LoggingSetup : public Catch::EventListenerBase {
public:
	using Catch::EventListenerBase::EventListenerBase;

	void testRunStarting(const Catch::TestRunInfo & /*testRunInfo*/) override {
		redis::logging::init(
			{.level    = test_log_level(),
			 .log_file = "",
			 .channels = redis::logging::channel_names(),
			 .async    = false});
	}

	void testRunEnded(const Catch::TestRunStats & /*testRunStats*/) override {
		redis::logging::shutdown();
	}
};
} // namespace

CATCH_REGISTER_LISTENER(LoggingSetup)
