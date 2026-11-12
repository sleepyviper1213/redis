// shutdown() with the async logger, as the server ends: everything queued
// must reach the file, and under ThreadSanitizer the logger must be freed in
// an order it can see (the worker is joined before the last reference goes).
#include "core/logging.hpp"
#include "server/log_channels.hpp"
#include "support/temp_file.hpp"

#include <catch2/catch_test_macros.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {
/// The suite's own logging, as logging.setup.cpp installs it: synchronous and
/// silent.
void restore_test_logging() {
	redis::logging::init({.level    = "off",
						  .log_file = "",
						  .channels = redis::logging::channel_names(),
						  .async    = false});
}
} // namespace

TEST_CASE("shutting down the async logger writes out everything queued",
		  "[logging]") {
	redis::logging::shutdown(); // the suite's logger
	const redis::test::TempFile file;
	redis::logging::init({.level    = "warn",
						  .log_file = file.path().string(),
						  .channels = redis::logging::channel_names(),
						  .async    = true});

	constexpr int LINES = 200;
	for (int i = 0; i < LINES; ++i) spdlog::warn("line {} of {}", i, LINES);
	redis::logging::shutdown();

	std::ifstream in(file.path());
	std::stringstream text;
	text << in.rdbuf();
	in.close();
	restore_test_logging();

	for (int i = 0; i < LINES; ++i)
		CHECK(text.str().find("line " + std::to_string(i) + " of") !=
			  std::string::npos);
}
