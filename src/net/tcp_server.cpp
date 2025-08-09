#include "tcp_server.hpp"

#include "formatter.hpp"
#include "nothrow_awaitable_t.hpp"
#include "redis_store.hpp"
#include "session.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace redis {
Server::Server(net::ip::port_type port_num)
	: acceptor_(ctx_, net::ip::tcp::endpoint(net::ip::address_v4::loopback(),
											 port_num)),
	  signalSet(ctx_, SIGINT, SIGTERM),
	  work_guard_(net::make_work_guard(ctx_)) {
	// Capture SIGINT and SIGTERM to perform a clean shutdown
	signalSet.async_wait(
		[this](const boost::system::error_code &error, unsigned long) {
			if (error) return;
			// A signal occurred, stop the `io_context`. This will cause
			// `run()` to return immediately, eventually destroying the
			// `io_context` and all of the sockets in it.
			ctx_.stop();
		});
}

void Server::start() noexcept {
	boost::system::error_code error;
	spdlog::info("[Server] Starting at {}", acceptor_.local_endpoint(error));
	if (error) return spdlog::error("[Server] Unable to start! {}", error);

	// Ignore the exception thrown as most errors are handled with
	// error_code
	net::co_spawn(ctx_, accept_incoming_connections(), net::detached);

	const uint32_t MAX_THREADS = 4;
	for (uint32_t i = 0; i < MAX_THREADS; ++i)
		net::post(thread_pool_, [this]() { ctx_.run(); });
}

net::awaitable<void> Server::accept_incoming_connections() noexcept {
	spdlog::debug("[Server] Waiting for a client to connect");
	for (RedisStore store;;) {
		auto [err, client] = co_await acceptor_.async_accept(nothrow_awaitable);
		if (err) {
			spdlog::error("[Server] Accept failed", err);
			net::steady_timer timer(co_await net::this_coro::executor);
			using namespace std::chrono_literals;
			timer.expires_after(100ms);
			co_await timer.async_wait(nothrow_awaitable);
		}
		spdlog::info("[Server] Accepted: {}", client.remote_endpoint());

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
	thread_pool_.stop();
}

} // namespace redis
