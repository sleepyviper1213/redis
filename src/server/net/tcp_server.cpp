#include "tcp_server.hpp"

#include "formatter.hpp"
#include "memory/database.hpp"
#include "session.hpp"

#include <utility>

namespace redis {

TcpListener TcpListener::from(net::io_context &ioc, const std::string &host,
							  net::ip::port_type port, Database &db) {
	net::ip::tcp::endpoint endpoint(net::ip::make_address_v4(host), port);

	return TcpListener{net::ip::tcp::acceptor(ioc, endpoint), db};
}

TcpListener::TcpListener(net::ip::tcp::acceptor acceptor, Database &db)
	: acceptor_(std::move(acceptor)), database_(db) {}

net::awaitable<void> TcpListener::start() noexcept {
	logger_->info("Listening at {}\n", acceptor_.local_endpoint());

	while (true) {
		auto [err, client] = co_await acceptor_.async_accept();
		if (err) logger_->error("Accept failed", err);

		const auto session =
			std::make_shared<Session>(std::move(client), database_, handler_);
		session->run();
	}
}

void TcpListener::stop() noexcept {
	logger_->info("Stopping server gracefully...");
	acceptor_.close();
}

} // namespace redis
