#pragma once
#include "redis_store.hpp"
#include "resp_parser.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstring> // For memmove

namespace net = boost::asio;

namespace redis {
class Session : public std::enable_shared_from_this<Session> {
public:
	Session(net::ip::tcp::socket socket, RedisStore &store);

	net::awaitable<void> run();


private:
	net::ip::tcp::socket socket_;
	RedisStore &store_;
	RespParser parser_;
};
} // namespace redis
