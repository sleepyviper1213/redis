#include "CLI/CLI.hpp"
#include "core/logging.hpp"
#include "server/diagnostics/crash.hpp"
#include "server/net/redis_server.hpp"
#include "server/setup_cli_options.hpp"

#include <fmt/base.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {
int current_pid() {
#ifdef _WIN32
	return _getpid();
#else
	return static_cast<int>(getpid());
#endif
}

int run(int argc, char **argv) {
	const auto app = redis::setup_cli_options();
	CLI11_PARSE(*app, argc, argv);

	// Shuts logging down (joining the async writer) on every return path.
	const redis::logging::guard log_guard{{
		.level    = app->get_option("--log-level")->as<std::string>(),
		.log_file = app->get_option("--log-file")->as<std::string>(),
	}};
	auto &log = redis::logging::logger_for(redis::logging::channel::server);
	redis::diagnostics::install_crash_handler(
		app->get_option("--log-file")->as<std::string>());

	// Who was running, and how: what an operator compares against a crash
	// report or a core dump.
	log.info("Server started, pid {}, port {}, {} I/O thread(s), dump file {}",
			 current_pid(),
			 app->get_option("--port")->as<uint16_t>(),
			 app->get_option("--io-threads")->as<unsigned>(),
			 app->get_option("--dump")->as<std::string>());

	if (app->count("--config") != 0u)
		log.info("Using config file: {}",
				 app->get_option("--config")->as<std::string>());

	try {
		redis::RedisServer server(redis::ServerConfig{
			.host            = app->get_option("--host")->as<std::string>(),
			.port            = app->get_option("--port")->as<uint16_t>(),
			.dump_file       = app->get_option("--dump")->as<std::string>(),
			.io_threads      = app->get_option("--io-threads")->as<unsigned>(),
			.sender_sessions = app->get_option("--sender-sessions")->as<bool>(),
		});
		server.run();
	} catch (const std::exception &e) {
		// e.g. boost::system::system_error when the port is already in use.
		log.critical("{}", e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
} // namespace

int main(int argc, char **argv) {
	// Whatever fails before logging is up (options, log file) has nowhere to
	// be logged: report it on stderr instead of letting it end the process.
	try {
		return run(argc, argv);
	} catch (const std::exception &e) {
		fmt::println(stderr, "fatal: {}", e.what());
		return EXIT_FAILURE;
	}
}
