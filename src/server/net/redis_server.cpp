#include "server/net/redis_server.hpp"

#include "server/net/tcp_server.hpp"

#include <boost/asio/co_spawn.hpp>

namespace redis {
RedisServer::RedisServer(std::string host, uint_least16_t port)
	: host_(std::move(host)),
	  port_(port),
	  io_(),
	  signals_(io_, SIGINT, SIGTERM) {}

void RedisServer::start() {
	logger_->info("Server initialized");
	auto server = TcpServer::from(io_, host_, port_);

	signals_.async_wait([&](auto, auto last_sig_received) {
		logger_->info("Received {}", strsignal(last_sig_received));
		server.stop();
		stop();
	});

	boost::asio::co_spawn(io_, server.start(), [](std::exception_ptr p) {
		if (p) std::rethrow_exception(p);
	});
	io_.run();
}

void RedisServer::stop() noexcept { io_.stop(); }

bool RedisServer::is_running() const noexcept { return !io_.stopped(); }

RedisServer::~RedisServer() { stop(); }
} // namespace redis
