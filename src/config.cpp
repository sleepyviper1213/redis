#include "config.hpp"

namespace redis {
redis::Config read(int argc, char **argv) {
	redis::Config cfg;

	CLI::App app{"Redis server"};

	// Flat mapping of struct members to CLI options
	app.add_option("--use_ssl", cfg.use_ssl);
	app.add_option("--host", cfg.addr.host);
	app.add_option("-p,--port", cfg.addr.port);
	app.set_config("--config", "config.toml", "Read an ini file", false);
	app.parse(argc, argv);
	return cfg;
}
} // namespace redis
