#include "core/logging.hpp"
#include "server/net/redis_server.hpp"
#include "server/setup_cli_options.hpp"

#include <cstdlib>
#include <exception>

int main(int argc, char **argv) {
	const auto app = redis::setup_cli_options();
	CLI11_PARSE(*app, argc, argv);

	// Shuts logging down (joining the async writer) on every return path.
	const redis::logging::guard log_guard{{
		.level    = app->get_option("--log-level")->as<std::string>(),
		.log_file = app->get_option("--log-file")->as<std::string>(),
	}};
	auto &log = redis::logging::logger_for(redis::logging::channel::server);

	if (app->count("--config"))
		log.info("Using config file: {}",
				 app->get_option("--config")->as<std::string>());

	try {
		redis::RedisServer server(redis::ServerConfig{
			.host      = app->get_option("--host")->as<std::string>(),
			.port      = app->get_option("--port")->as<uint16_t>(),
			.dump_file = app->get_option("--dump")->as<std::string>(),
			.io_threads = app->get_option("--io-threads")->as<unsigned>(),
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
