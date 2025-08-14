#include "co_main.hpp"

#include "config.hpp"
#include "nothrow_awaitable_t.hpp"
#include "tcp_server.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/signal_set.hpp>
#include <spdlog/spdlog.h>

#include <cstring>

namespace redis {
net::awaitable<void> co_main(Config cfg) {
	spdlog::set_level(spdlog::level::debug);
	auto ex = co_await net::this_coro::executor;

	auto server = Server::from(ex, cfg);

	co_await server.start();

	net::signal_set signalSet(ex, SIGINT, SIGTERM);
	signalSet.async_wait([&server](auto, auto sig_num) {
		spdlog::info("[Server] Received signal {}", strsignal(sig_num));
		server.stop();
	});
}
} // namespace redis
