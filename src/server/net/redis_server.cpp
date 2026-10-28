#include "server/net/redis_server.hpp"

#include "core/logging.hpp"
#include "server/net/formatter.hpp"
#include "server/net/tcp_server.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>

#include <csignal>
#include <exception>
#include <string_view>
#include <utility>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::server);
}

std::string_view signal_name(int sig) {
	switch (sig) {
	case SIGINT: return "SIGINT";
	case SIGTERM: return "SIGTERM";
	default: return "unknown signal";
	}
}
} // namespace

RedisServer::RedisServer(ServerConfig config)
	: config_(std::move(config)),
	  signals_(io_, SIGINT, SIGTERM),
	  persistence_(config_.dump_file) {}

void RedisServer::run() {
	if (auto loaded = persistence_.load(db_); !loaded) {
		// Refuse to start rather than serve an empty keyspace and later
		// overwrite the unreadable snapshot with it.
		log().critical("{}", loaded.error());
		throw std::runtime_error(loaded.error());
	}

	TcpServer server(io_, config_.host, config_.port, handler_, ctx_);

	signals_.async_wait([&](const boost::system::error_code &ec, int sig) {
		if (ec) return;
		log().info("Received {}, shutting down", signal_name(sig));
		server.stop();
		io_.stop();
	});

	auto rethrow = [](const std::exception_ptr &e) {
		if (e) std::rethrow_exception(e);
	};
	net::co_spawn(io_, server.start(), rethrow);
	net::co_spawn(io_, cron(), rethrow);

	log().info("Ready to accept connections");
	io_.run();

	if (auto saved = persistence_.save(db_); !saved)
		log().error("Final snapshot failed: {}", saved.error());
	log().info("Redis is now ready to exit, bye bye...");
}

net::awaitable<void> RedisServer::cron() {
	net::steady_timer timer(io_);
	while (true) {
		timer.expires_after(CRON_PERIOD);
		if (auto [ec] = co_await timer.async_wait(); ec) co_return;

		if (const auto expired = db_.active_expire_cycle(EXPIRE_KEYS_PER_RUN))
			log().trace("Expired {} keys", expired);
		persistence_.poll();
	}
}

} // namespace redis
