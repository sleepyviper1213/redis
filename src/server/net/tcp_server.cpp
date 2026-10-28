#include "server/net/tcp_server.hpp"

#include "core/logging.hpp"
#include "server/net/formatter.hpp"
#include "server/net/session.hpp"

#include <memory>
#include <utility>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::net);
}
} // namespace

TcpServer::TcpServer(net::io_context &ioc, const std::string &host,
					 net::ip::port_type port, const CommandHandler &handler,
					 CommandContext &ctx)
	: acceptor_(ioc,
				net::ip::tcp::endpoint(net::ip::make_address_v4(host), port)),
	  handler_(handler),
	  ctx_(ctx) {}

net::awaitable<void> TcpServer::start() {
	log().info("Listening at {}", acceptor_.local_endpoint());

	while (acceptor_.is_open()) {
		auto [err, client] = co_await acceptor_.async_accept();
		if (err == net::error::operation_aborted) co_return; // stop()
		if (err) {
			// Transient failures (e.g. EMFILE when out of descriptors) must
			// not kill the accept loop, and the failed socket is unusable.
			log().error("Accept failed: {}", err);
			continue;
		}
		std::make_shared<Session>(std::move(client), handler_, ctx_)->run();
	}
}

void TcpServer::stop() noexcept {
	boost::system::error_code ignored;
	acceptor_.close(ignored);
}

} // namespace redis
