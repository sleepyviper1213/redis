#include "config.hpp"
#include "net.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <spdlog/spdlog.h>

#include <csignal>

int main(int argc, char **argv) {
	const auto logger = redis::make_logger("main");
	const auto app    = redis::configure_from_cli(argc, argv);
	try {
		app->parse(argc, argv);
		if (!app->count("--config"))
			spdlog::warn("No config file specified. Using default config");
		else
			spdlog::warn("Using config file: {}",
						 app->get_option("--config")->as<std::string_view>());
		spdlog::set_level(spdlog::level::from_str(
			app->get_option("--loglevel")->as<std::string>()));

		boost::asio::io_context ioc;
		boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
		auto server = redis::Server::from(ioc, app.get());
		signals.async_wait([&](auto, auto last_sig_received) {
			logger->info("Received {}", strsignal(last_sig_received));
			server.stop();
			ioc.stop();
		});
		boost::asio::co_spawn(ioc, server.start(), [](std::exception_ptr p) {
			if (p) std::rethrow_exception(p);
		});
		ioc.run();
	} catch (const CLI ::ParseError &e) {
		return app->exit(e);
	} catch (const boost::system::error_code &e) {
		logger->critical("{}", e.what());
	}
}
