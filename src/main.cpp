#include "config.hpp"
#include "net.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
	const auto logger = redis::make_logger("main");
	try {
		auto cfg = redis::configure_from_cli(argc, argv);

		boost::asio::io_context ioc;
		boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
		auto server = redis::Server::from(ioc, cfg);
		signals.async_wait([&](auto, auto) {
			server.stop();
			ioc.stop();
		});
		boost::asio::co_spawn(ioc, server.start(), [](std::exception_ptr p) {
			if (p) std::rethrow_exception(p);
		});
		ioc.run();

	} catch (const boost::system::system_error &e) {
		logger->error("{}", e.what());
		return 1;
	}
}
