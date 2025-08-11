#pragma once
#include "redis_store.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <memory>

namespace net = boost::asio;

namespace redis {
class Session : public std::enable_shared_from_this<Session> {
public:
	Session(net::ip::tcp::socket socket, RedisStore &store);

	Session(const Session &)            = delete;
	Session(Session &&)                 = default;
	Session &operator=(const Session &) = delete;
	Session &operator=(Session &&)      = delete;

	net::awaitable<void> run();


private:
	net::ip::tcp::socket socket_;
	RedisStore &store_;
};
} // namespace redis
