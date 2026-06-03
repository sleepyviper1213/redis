#pragma once
#include "core.hpp"
#include "memory/database.hpp"
#include "nothrow_awaitable_t.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <filesystem>
#include <string>

namespace redis {
namespace net = boost::asio;

class RedisServer {
public:
	RedisServer(std::string host, uint_least16_t port);

	void start();

	void stop() noexcept;

	[[nodiscard]] bool is_running() const noexcept;

	// Persistence
	void save_snapshot(const std::filesystem::path &file) const;
	void load_snapshot(const std::filesystem::path &file);

	/**
	 * \brief Write in-memory cache to disk
	 */
	void save_db();


private:
	std::string host_;
	int port_;
	net::io_context io_;
	boost::asio::signal_set signals_;
	Database db_; // your in-memory key-value store

	CLASS_LOGGER(RedisServer);
};
} // namespace redis
