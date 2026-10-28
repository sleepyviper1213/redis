#pragma once
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep
#include "server/commands/command.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <string>

namespace redis {
class CommandHandler;
class Executor;

namespace net = boost::asio;

inline namespace v1 {
/**
 * \brief Accepts TCP connections and starts a Session for each.
 *
 * Safe to run on several I/O threads: the acceptor lives on its own strand and
 * every session gets one of its own.
 */
class [[nodiscard]] TcpServer {
public:
	/// Bind and listen on \p host:\p port (IPv4). Throws on failure, e.g.
	/// when the port is in use. \p executor, \p handler and \p ctx must
	/// outlive every session.
	TcpServer(net::io_context &ioc, const std::string &host,
			  net::ip::port_type port, Executor &executor,
			  const CommandHandler &handler, CommandContext &ctx);

	/// Accept connections until stop() is called.
	net::awaitable<void> start();

	/// Stop accepting. Established sessions keep running. May be called from
	/// any thread.
	void stop() noexcept;

private:
	net::io_context &ioc_;
	net::ip::tcp::acceptor acceptor_;
	Executor &executor_;
	const CommandHandler &handler_;
	CommandContext &ctx_;
};
} // namespace v1

} // namespace redis