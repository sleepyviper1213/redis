#include "core/logging.hpp"
#include "server.hpp"

#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
	const auto app = redis::setup_cli_options_from(argc, argv);

	CLI11_PARSE(*app, argc, argv);
	auto log_level = spdlog::level::from_str(
		app->get_option("--log-level")->as<std::string>());
	redis::Logger::get_instance().init(
		app->get_option("--log-file")->as<std::string>(),
		log_level);

	auto logger = redis::Logger::get_instance().get("main");

	try {
		if (!app->count("--config"))
			logger->warn("No config file specified. Using default config");
		else
			logger->warn("Using config file: {}",
						 app->get_option("--config")->as<std::string_view>());
		redis::RedisServer server(
			app->get_option("--host")->as<std::string>(),
			app->get_option("--port")->as<uint_least16_t>());
		server.start();
	} catch (const boost::system::error_code &e) {
		logger->critical("{}", e.what());
	}
}
