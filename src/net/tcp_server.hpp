#pragma once
#include "net/nothrow_awaitable_t.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace redis {
namespace net = boost::asio;

class Config;

/**
 * \brief A simple TCP server
 */
class [[nodiscard]] Server {
public:
	static Server from(net::any_io_executor &ioc, const Config &cfg);

	/**
	 * \brief Accept the connection request asynchronously.
	 */
	net::awaitable<void> start() noexcept;

	/**
	 * \brief Cancel all runnning services before terminating.
	 */
	void stop() noexcept;

protected:
	/**
	 * \brief Construct the server listening to port \a port_num one the local
	 * host using IPv4 address
	 */
	explicit Server(net::ip::tcp::acceptor acceptor);

private:
	net::ip::tcp::acceptor acceptor_;
};
} // namespace redis
