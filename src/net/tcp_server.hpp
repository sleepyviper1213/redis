#pragma once
#include "net/database.hpp"
#include "net/nothrow_awaitable_t.hpp"
#include "utils/get_class_logger.hpp"

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
	static Server from(net::io_context &ioc, const Config &cfg);
	/**
	 * \brief Construct the server listening to port \a port_num one the local
	 * host using IPv4 address
	 */
	explicit Server(net::ip::tcp::acceptor acceptor);


	/**
	 * \brief Accept the connection request asynchronously.
	 */
	net::awaitable<void> start() noexcept;

	/**
	 * \brief Cancel all runnning services before terminating.
	 */
	void stop() noexcept;
	void saveDB() const;

private:
	Database database_;
	net::ip::tcp::acceptor acceptor_;
	CLASS_LOGGER(Server);
};
} // namespace redis
