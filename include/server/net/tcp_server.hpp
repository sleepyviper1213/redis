#pragma once
#include "nothrow_awaitable_t.hpp"
#include "server/commands/handler.hpp"
#include "server/memory/database.hpp"
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
class [[nodiscard]] TcpServer {
public:
	static TcpServer from(net::io_context &ioc, const std::string &host,
						  net::ip::port_type port);
	/**
	 * \brief Construct the server listening to port \a port_num one the local
	 * host using IPv4 address
	 */
	explicit TcpServer(net::ip::tcp::acceptor acceptor);

	/**
	 * \brief Accept the connection request asynchronously.
	 */
	net::awaitable<void> start() noexcept;

	/**
	 * \brief Cancel all runnning services before terminating.
	 */
	void stop() noexcept;

	/**
	 * \brief Write in-memory cache to disk
	 */
	void save_db();


private:
	net::ip::tcp::acceptor acceptor_;
	std::vector<Database> database_;
	CommandHandler handler_;
	// std::vector<saveparam>param;
	CLASS_LOGGER(TcpServer);
};
} // namespace redis
