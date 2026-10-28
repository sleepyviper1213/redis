#pragma once
// Must precede every Asio include: makes co_await on Asio operations return
// (error_code, result) tuples.
#include "server/net/nothrow_awaitable_t.hpp" // IWYU pragma: keep

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
};

/**
 * \brief The composition root: owns the keyspace, persistence, the command
 * table and the event loop.
 *
 * Everything runs on one thread, like Redis: commands execute one at a time,
 * so the keyspace needs no locks.
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
};
} // namespace redis
