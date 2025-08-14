#include "tcp_server.hpp"

#include "formatter.hpp"
#include "config.hpp"
#include "redis_store.hpp"
#include "session.hpp"

#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace redis {

Server Server::from(net::any_io_executor &ioc, const Config &cfg) {
	net::ip::tcp::endpoint endpoint(net::ip::make_address_v4(cfg.addr.host),
									cfg.addr.port);

	return Server{net::ip::tcp::acceptor(ioc, endpoint)};
}

Server::Server(net::ip::tcp::acceptor acceptor)
	: acceptor_(std::move(acceptor)) {}

net::awaitable<void> Server::start() noexcept {
	spdlog::info("[Server] Starting at {}", acceptor_.local_endpoint());

	spdlog::info("[Server] Accepting TCP connections");
	for (RedisStore store;;) {
		auto [err, client] = co_await acceptor_.async_accept();
		if (err) spdlog::error("[Server] Accept failed", err);

		const auto session =
			std::make_shared<Session>(std::move(client), store);
		co_await session->run();
	}
}

void Server::stop() noexcept {
	spdlog::info("[Server] Stopping server...");
	boost::system::error_code err;
	if (acceptor_.cancel(err); err)
		return spdlog::error("[Server] Stop: {}", err);

	acceptor_.close();
	spdlog::info("[Server] Stopped!");
	// thread_pool_.stop();
}

} // namespace redis
