#include "tcp_server.hpp"

#include "database.hpp"
#include "formatter.hpp"
#include "session.hpp"

#include <CLI/App.hpp>
#include <spdlog/logger.h>

#include <utility>

namespace redis {

Server Server::from(net::io_context &ioc, const CLI::App *cfg) {
	net::ip::tcp::endpoint endpoint(
		net::ip::make_address_v4(cfg->get_option("--host")->as<std::string>()),
		cfg->get_option("--port")->as<int>());

	return Server{net::ip::tcp::acceptor(ioc, endpoint)};
}

Server::Server(net::ip::tcp::acceptor acceptor)
	: acceptor_(std::move(acceptor)) {
	for (size_t i = 0; i < 2; i++) database_.emplace_back(i);
}

net::awaitable<void> Server::start() noexcept {
	logger_->info("Starting at {}", acceptor_.local_endpoint());
	logger_->info("Accepting TCP connections");

	while (true) {
		auto [err, client] = co_await acceptor_.async_accept();
		if (err) logger_->error("Accept failed", err);

		const auto session =
			std::make_shared<Session>(std::move(client), database_[0]);
		session->run();
	}
}

void Server::saveDB() {
	logger_->info("Saving the final RDB snapshot before exiting");

	for (auto &db : database_) db.save();
}

void Server::stop() noexcept {
	logger_->info("Stopping server gracefully...");
	saveDB();
	acceptor_.close();
	logger_->info("Redis is now ready to exit");
	// thread_pool_.stop();
}

} // namespace redis
