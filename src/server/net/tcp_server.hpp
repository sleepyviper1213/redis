#pragma once
// Makes co_await on Asio operations return (error_code, result) tuples.
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep
#include "server/commands/command.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <string>

namespace redis {
class CommandHandler;

namespace net = boost::asio;

/**
 * \brief Accepts TCP connections and starts a Session for each.
 */
class [[nodiscard]] TcpServer {
public:
	/// Bind and listen on \p host:\p port (IPv4). Throws on failure, e.g.
	/// when the port is in use.
	TcpServer(net::io_context &ioc, const std::string &host,
			  net::ip::port_type port, const CommandHandler &handler,
			  CommandContext &ctx);

	/// Accept connections until stop() is called.
	net::awaitable<void> start();

	/// Stop accepting. Established sessions keep running.
	void stop() noexcept;

private:
	net::ip::tcp::acceptor acceptor_;
	const CommandHandler &handler_;
	CommandContext &ctx_;
};
} // namespace redis
