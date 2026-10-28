#include "redis_server.hpp"

#include "core/logging.hpp"
#include "formatter.hpp"
#include "tcp_server.hpp"
#include "v2/tcp_server.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exec/start_detached.hpp>

#include <algorithm>
#include <csignal>
#include <exception>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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
	  io_(static_cast<int>(std::max(1U, config_.io_threads))),
	  signals_(io_, SIGINT, SIGTERM),
	  persistence_(config_.dump_file),
	  executor_(config_.io_threads > 1) {}

void RedisServer::run() {
	if (auto loaded = persistence_.load(db_); !loaded) {
		// Refuse to start rather than serve an empty keyspace and later
		// overwrite the unreadable snapshot with it.
		log().critical("{}", loaded.error());
		throw std::runtime_error(loaded.error());
	}

	// Exactly one front end: binding the port twice would fail.
	std::optional<TcpServer> server;
	std::optional<v2::TcpServer> sender_server;
	if (config_.sender_sessions)
		sender_server.emplace(io_, config_.host, config_.port, executor_,
							  handler_, ctx_);
	else
		server.emplace(io_, config_.host, config_.port, executor_, handler_,
					   ctx_);

	// Leave the event loop, once. Coroutine sessions are simply dropped with
	// it. Sender sessions live in a scope that must not be destroyed while
	// they run: cancel them and stop the loop only after they have unwound.
	std::once_flag shutting_down;
	const auto shut_down = [&] {
		std::call_once(shutting_down, [&] {
			if (!sender_server) {
				server->stop();
				io_.stop();
				return;
			}
			sender_server->stop();
			exec::start_detached(sender_server->drained()
								 | stdexec::then([this] { io_.stop(); }));
		});
	};

	signals_.async_wait([&](const boost::system::error_code &ec, int sig) {
		if (ec) return;
		log().info("Received {}, shutting down", signal_name(sig));
		shut_down();
	});

	// A failure on any I/O thread stops them all and is rethrown from here,
	// as it would be from a single-threaded io_.run().
	std::exception_ptr failure;
	std::once_flag failed;
	auto record = [&](std::exception_ptr e) {
		std::call_once(failed, [&] { failure = std::move(e); });
		shut_down();
	};
	auto rethrow = [&](const std::exception_ptr &e) {
		if (e) record(e);
	};
	if (sender_server) sender_server->start();
	else net::co_spawn(io_, server->start(), rethrow);
	net::co_spawn(io_, cron(), rethrow);

	log().info("Ready to accept connections");
	{
		const auto run_io = [&] {
			try {
				io_.run();
			} catch (...) {
				record(std::current_exception());
			}
		};
		std::vector<std::jthread> workers;
		for (unsigned i = 1; i < config_.io_threads; ++i)
			workers.emplace_back(run_io);
		run_io();
	} // joins the workers
	if (failure) std::rethrow_exception(failure);

	// A batch may still be executing: wait for it before reading the keyspace.
	executor_.stop();

	if (auto saved = persistence_.save(db_); !saved)
		log().error("Final snapshot failed: {}", saved.error());
	log().info("Redis is now ready to exit, bye bye...");
}

net::awaitable<void> RedisServer::cron() {
	net::steady_timer timer(io_);
	while (true) {
		timer.expires_after(CRON_PERIOD);
		if (auto [ec] = co_await timer.async_wait(); ec) co_return;

		// Touches the keyspace, so it runs where commands run.
		co_await executor_.run([this] {
			if (const auto expired =
					db_.active_expire_cycle(EXPIRE_KEYS_PER_RUN))
				log().trace("Expired {} keys", expired);
			persistence_.poll();
		});
	}
}

} // namespace redis
