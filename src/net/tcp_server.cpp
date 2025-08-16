#include "tcp_server.hpp"

#include "config.hpp"
#include "database.hpp"
#include "formatter.hpp"
#include "session.hpp"

#include <spdlog/logger.h>

#include <memory>
#include <utility>

namespace redis {

Server Server::from(net::io_context &ioc, const Config &cfg) {
	net::ip::tcp::endpoint endpoint(net::ip::make_address_v4(cfg.addr.host),
									cfg.addr.port);

	return Server{net::ip::tcp::acceptor(ioc, endpoint)};
}

Server::Server(net::ip::tcp::acceptor acceptor)
	: acceptor_(std::move(acceptor)) {}

net::awaitable<void> Server::start() noexcept {
	logger_->info("Starting at {}", acceptor_.local_endpoint());

	logger_->info("Accepting TCP connections");
	while (true) {
		auto [err, client] = co_await acceptor_.async_accept();
		if (err) logger_->error("Accept failed", err);

		const auto session =
			std::make_shared<Session>(std::move(client), database_);
		co_await session->loop();
	}
}

void Server::saveDB() const {
	logger_->info("Saving the final RDB snapshot before exiting");
	database_.save();
}

void Server::stop() noexcept {
	logger_->info("Stopping server...");
	saveDB();
	acceptor_.close();
	logger_->info("Redis is now ready to exit");
	// thread_pool_.stop();
}

} // namespace redis
