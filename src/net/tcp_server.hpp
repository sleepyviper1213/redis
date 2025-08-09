#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>

namespace redis {
namespace net = boost::asio;

/**
 * \brief A simple TCP server
 */
class [[nodiscard]] Server {
public:
	/**
	 * \brief Construct the server listening to port \a port_num one the local
	 * host using IPv4 address
	 */
	explicit Server(net::ip::port_type port_num);

	/**
	 * \brief Accept the connection request asynchronously.
	 */
	void start() noexcept;

	/**
	 * \brief Cancel all runnning services before terminating.
	 */
	void stop() noexcept;

private:
	net::io_context ctx_;

	/**
	 * \brief Listen for client's TCP socket
	 *
	 */
	net::awaitable<void> accept_incoming_connections() noexcept;

	net::ip::tcp::acceptor acceptor_;
	net::signal_set signalSet;
	net::executor_work_guard<net::io_context::executor_type> work_guard_;
	net::thread_pool thread_pool_;
};
} // namespace redis
