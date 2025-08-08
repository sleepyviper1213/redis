#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/thread_pool.hpp>

namespace redis {

class CommandDispatcher;

namespace net = boost::asio;
using tcp     = net::ip::tcp;

/**
 * \brief A simple TCP server
 */
class [[nodiscard]] Server {
public:
	/**
	 * \brief Construct the server, but not open for connection yet
	 *
	 * \note When constructed this way, it is necessary to call \a
	 * open_connection_from() to start listening
	 */
	// explicit Server(tcp::acceptor acceptor);

	/**
	 * \brief Construct the server listening to port \a port_num one the local
	 * host using IPv4 address
	 */
	Server(net::ip::port_type port_num);

	/**
	 * \brief Accept the connection request asynchronously. Once accepted, try
	 * to load SSL certificates in this source directory and start a Websocket
	 * connection/
	 */
	void start() noexcept;


	/**
	 * \brief Cancel all runnning services before terminating.
	 */
	void stop() noexcept;

	net::awaitable<void> do_listen();

private:
	net::io_context ctx_;

	net::awaitable<void> accept_incoming_connections() noexcept;

	tcp::acceptor acceptor_;
	net::signal_set signalSet;
	net::executor_work_guard<net::io_context::executor_type> work_guard_;
	net::thread_pool thread_pool_;
};


} // namespace redis
