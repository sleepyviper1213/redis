#pragma once
#include "commands/handler.hpp"
#include "core/database.hpp"
#include "net/nothrow_awaitable_t.hpp"
// #include "saveparam.hpp"
#include "utils/get_class_logger.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <vector>

namespace CLI {
class App;
}

namespace redis {
namespace net = boost::asio;

/**
 * \brief A simple TCP server
 */
class [[nodiscard]] TcpServer {
public:
	static TcpServer from(net::io_context &ioc, const CLI::App *cfg);
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
	void saveDB();


private:
	net::ip::tcp::acceptor acceptor_;
	std::vector<Database> database_;
	CommandHandler handler_;
	// std::vector<saveparam>param;
	CLASS_LOGGER(TcpServer);
};
} // namespace redis
