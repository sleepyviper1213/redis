#include "core/logging.hpp"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {
// Quiet, synchronous logging for the whole run: no file, nothing below
// "off", and no async worker thread to outlive the test binary's main.
class LoggingSetup : public Catch::EventListenerBase {
public:
	using Catch::EventListenerBase::EventListenerBase;

	void testRunStarting(const Catch::TestRunInfo &) override {
		redis::logging::init(
			{.level = "off", .log_file = "", .async = false});
	}

	void testRunEnded(const Catch::TestRunStats &) override {
		redis::logging::shutdown();
	}
};
} // namespace

CATCH_REGISTER_LISTENER(LoggingSetup)
