#pragma once
#include "core/logging.hpp"
#include "server/net/nothrow_awaitable_t.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <string>

namespace redis {
namespace net = boost::asio;

class RedisServer {
public:
	RedisServer(std::string host, uint_least16_t port);

	void start();

	void stop() noexcept;

	[[nodiscard]] bool is_running() const noexcept;

	~RedisServer();

private:
	std::string host_;
	int port_;
	net::io_context io_;
	boost::asio::signal_set signals_;
	CLASS_LOGGER(RedisServer);
};
} // namespace redis
