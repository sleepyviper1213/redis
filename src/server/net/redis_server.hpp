#pragma once
// Must precede every Asio include: makes co_await on Asio operations return
// (error_code, result) tuples.
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep
#include "executor.hpp"

#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/persistence/persistence.hpp"
#include "server/storage/database.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace redis {
namespace net = boost::asio;

struct ServerConfig {
	std::string host = "127.0.0.1";
	uint16_t port    = 6379;
	std::filesystem::path dump_file = "dump.snap";
	/// Threads running network I/O. 1 runs everything on one thread; more
	/// adds a thread dedicated to executing commands (see Executor).
	unsigned io_threads = 1;
	/// Serve clients with the sender-based v2::Session instead of the
	/// coroutine-based Session.
	bool sender_sessions = false;
};

/**
 * \brief The composition root: owns the keyspace, persistence, the command
 * table and the event loop.
 *
 * Commands execute one at a time on a single thread, like Redis, so the
 * keyspace needs no locks. With ServerConfig::io_threads == 1 that is the one
 * thread that also does the I/O; with more, it is a dedicated thread and the
 * I/O threads only read, parse and write.
 */
class RedisServer {
public:
	explicit RedisServer(ServerConfig config);

	RedisServer(const RedisServer &)            = delete;
	RedisServer &operator=(const RedisServer &) = delete;

	/**
	 * \brief Load the snapshot, serve clients until SIGINT or SIGTERM, then
	 * save a final snapshot. Blocks.
	 * \throws boost::system::system_error if the port cannot be bound.
	 */
	void run();

private:
	/// Periodic housekeeping, like Redis' serverCron: active expiry and
	/// collecting finished background saves.
	net::awaitable<void> cron();

	/// How often cron() runs, and how many keys one expiry cycle may remove.
	static constexpr auto CRON_PERIOD          = std::chrono::milliseconds(100);
	static constexpr size_t EXPIRE_KEYS_PER_RUN = 1000;

	ServerConfig config_;
	net::io_context io_;
	net::signal_set signals_;

	Database db_;
	Persistence persistence_;
	CommandHandler handler_;
	CommandContext ctx_{db_, persistence_};
	/// Declared last so it is destroyed first: its thread, the only one
	/// touching the keyspace, is joined before the keyspace goes away.
	Executor executor_;
};
} // namespace redis
