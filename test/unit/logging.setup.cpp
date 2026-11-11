#include "core/logging.hpp"
#include "server/log_channels.hpp"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {
// Quiet, synchronous logging for the whole run: no file, nothing below
// "off", and no async worker thread to outlive the test binary's main.
class LoggingSetup : public Catch::EventListenerBase {
public:
	using Catch::EventListenerBase::EventListenerBase;

	void testRunStarting(const Catch::TestRunInfo & /*testRunInfo*/) override {
		redis::logging::init(
			{.level    = "off",
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
