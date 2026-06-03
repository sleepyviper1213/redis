#pragma once
#include "commands/handler.hpp"
#include "memory/database.hpp"
#include "nothrow_awaitable_t.hpp"
// #include "saveparam.hpp"
#include "core/logging.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <vector>

namespace redis {
namespace net = boost::asio;

/**
 * \brief A simple TCP server
 */
class [[nodiscard]] TcpListener {
public:
	static TcpListener from(net::io_context &ioc, const std::string &host,
							net::ip::port_type port, Database &db);
	/**
	 * \brief Construct the server listening to port \a port_num one the local
	 * host using IPv4 address
	 */
	TcpListener(net::ip::tcp::acceptor acceptor, Database &db);

	/**
	 * \brief Accept the connection request asynchronously.
	 */
	net::awaitable<void> start() noexcept;

	/**
	 * \brief Cancel all runnning services before terminating.
	 */
	void stop() noexcept;

private:
	net::ip::tcp::acceptor acceptor_;
	CommandHandler handler_;
	Database &database_;
	// std::vector<saveparam>param;
	CLASS_LOGGER(TcpServer);
};
} // namespace redis
