#include "redis_server.hpp"

#include "core/logging.hpp"
#include "formatter.hpp"
#include "listener.hpp"
#include "v2/listener.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exec/start_detached.hpp>

#include <algorithm>
#include <csignal>
#include <exception>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::server); }

// What an exception caught on an Asio thread says. Asio hands failures over as
// exception_ptrs, so the only way to read one is to throw it again.
std::string describe(const std::exception_ptr &error) {
	try {
		std::rethrow_exception(error);
	} catch (const std::exception &e) {
		return e.what();
	} catch (...) { return "unknown exception"; }
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
	  services_(std::make_shared<Services>(config_.dump_file,
										   config_.io_threads > 1,
										   config_.aof)) {}

std::expected<void, std::string> RedisServer::run() {
	// With an append-only file it is the source of truth: it holds everything
	// since the last rewrite, which the snapshot does not.
	const auto loaded = services_->aof
							? services_->aof->load(services_->ctx,
												   services_->handler)
							: services_->persistence.load(services_->db);
	if (!loaded) {
		// Refuse to start rather than serve an empty keyspace and later
		// overwrite the unreadable files with it.
		return std::unexpected(loaded.error());
	}

	// Exactly one front end: binding the port twice would fail.
	std::optional<Listener> server;
	std::optional<v2::Listener> sender_server;
	if (config_.sender_sessions) {
		sender_server.emplace(io_.get_executor(), services_);
		if (auto listening = sender_server->listen(config_.host, config_.port);
			!listening)
			return std::unexpected(listening.error());
	} else {
		server.emplace(io_.get_executor(), services_, config_.io_threads > 1);
		if (auto listening = server->listen(config_.host, config_.port);
			!listening)
			return std::unexpected(listening.error());
	}

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
			exec::start_detached(sender_server->drained() |
								 stdexec::then([this] { io_.stop(); }));
		});
	};

	signals_.async_wait([&](const boost::system::error_code &ec, int sig) {
		if (ec) return;
		log().info("Received {}, shutting down", signal_name(sig));
		shut_down();
	});

	// A failure on any I/O thread stops them all and is returned from here,
	// as it would be from a single-threaded io_.run(). Only the first is kept.
	std::optional<std::string> failure;
	std::once_flag failed;
	auto record = [&](std::string message) {
		std::call_once(failed, [&] { failure = std::move(message); });
		shut_down();
	};
	auto on_done = [&](const std::exception_ptr &error) {
		if (error) record(describe(error));
	};
	if (sender_server) sender_server->start();
	else net::co_spawn(io_, server->start(), on_done);
	net::co_spawn(io_, cron(), on_done);

	log().info("Ready to accept connections");
	{
		const auto run_io = [&] {
			// Asio handlers can throw; this is where that ends.
			try {
				io_.run();
			} catch (const std::exception &e) {
				record(e.what());
			} catch (...) { record("unknown exception"); }
		};
		std::vector<std::jthread> workers;
		for (unsigned i = 1; i < config_.io_threads; ++i)
			workers.emplace_back(run_io);
		run_io();
	} // joins the workers
	if (failure) return std::unexpected(*failure);

	// A batch may still be executing: wait for it before reading the keyspace.
	services_->keyspace.stop();

	// A running BGSAVE would make the final save below fail ("already in
	// progress"), and its snapshot predates the latest writes anyway.
	services_->persistence.cancel_background_save();

	// Nothing runs commands any more: what is logged is final. An unfinished
	// rewrite is dropped; the files it would have replaced are still whole.
	if (services_->aof) {
		services_->aof->cancel_rewrite();
		services_->aof->close();
	}

	// Redis refuses to exit when this fails, and keeps serving. This server
	// cannot resume once its loop has stopped, so it fails loudly instead: the
	// error is returned and main() exits non-zero, which a supervisor sees,
	// rather than a clean shutdown that lost the data.
	if (auto saved = services_->persistence.save(services_->db); !saved)
		return std::unexpected("final snapshot failed: " + saved.error());
	log().info("Redis is now ready to exit, bye bye...");
	return {};
}

net::awaitable<void> RedisServer::cron() {
	net::steady_timer timer(co_await net::this_coro::executor);
	const auto services = services_;
	while (true) {
		timer.expires_after(CRON_PERIOD);
		if (auto [ec] = co_await timer.async_wait(); ec) co_return;

		// Touches the keyspace, so it runs where commands run.
		co_await services->keyspace.run([&services] {
			if (const auto expired =
					services->db.active_expire_cycle(EXPIRE_KEYS_PER_RUN))
				log().trace("Expired {} keys", expired);
			services->persistence.poll();
			if (services->aof) {
				// The expiry above, and the writes since the last batch, if
				// any, need to reach the file too.
				services->aof->log_expired(services->db);
				services->aof->after_batch();
				services->aof->poll();
			}
		});
	}
}

} // namespace redis
