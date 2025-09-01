#include "server/net/tcp_server.hpp"

#include "server/memory/database.hpp"
#include "server/net/formatter.hpp"
#include "server/net/session.hpp"

#include <utility>

namespace redis {

TcpServer TcpServer::from(net::io_context &ioc, const std::string &host,
						  net::ip::port_type port) {
	net::ip::tcp::endpoint endpoint(net::ip::make_address_v4(host), port);

	return TcpServer{net::ip::tcp::acceptor(ioc, endpoint)};
}

TcpServer::TcpServer(net::ip::tcp::acceptor acceptor)
	: acceptor_(std::move(acceptor)) {
	for (size_t i = 0; i < 2; i++) database_.emplace_back(i);
}

net::awaitable<void> TcpServer::start() noexcept {
	logger_->info("Listening at {}", acceptor_.local_endpoint());
	logger_->info("Accepting TCP connections");

	while (true) {
		auto [err, client] = co_await acceptor_.async_accept();
		if (err) logger_->error("Accept failed", err);

		const auto session = std::make_shared<Session>(std::move(client),
													   database_[0],
													   handler_);
		session->run();
	}
}

void TcpServer::saveDB() {
	logger_->info("Saving the final RDB snapshot before exiting");

	for (auto &db : database_) db.save();
}

void TcpServer::stop() noexcept {
	logger_->info("Stopping server gracefully...");
	saveDB();
	acceptor_.close();
	logger_->info("Redis is now ready to exit");
	// thread_pool_.stop();
}

} // namespace redis
