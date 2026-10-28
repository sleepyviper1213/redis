#pragma once

#include "server/net/nothrow_awaitable_t.hpp" // IWYU pragma: keep

#include "server/commands/command.hpp"
#include "asio_context.hpp"
#include "session.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <stdexec/execution.hpp>

#include <string>

namespace redis {
class CommandHandler;
class Executor;

namespace v2 {

/**
 * \brief Accepts TCP connections and serves each with a v2::Session.
 *
 * The accept loop and every session are senders started into one counting
 * scope, which owns them: stop() cancels them all through the scope's stop
 * token, and drained() completes once they have finished unwinding. The
 * server must be drained before it is destroyed.
 *
 * Safe to run on several I/O threads: the acceptor lives on its own strand
 * and every session gets one of its own.
 */
class [[nodiscard]] TcpServer {
public:
	/// Bind and listen on \p host:\p port (IPv4). Throws on failure, e.g.
	/// when the port is in use. \p executor, \p handler and \p ctx must
	/// outlive every session.
	TcpServer(boost::asio::io_context &ioc, const std::string &host,
			  boost::asio::ip::port_type port, Executor &executor,
			  const CommandHandler &handler, CommandContext &ctx);

	/// Start accepting. Returns at once; the loop runs on the I/O threads.
	void start();

	/// Stop accepting and cancel every session. May be called from any
	/// thread. Follow with drained().
	void stop() noexcept;

	/// Completes once the accept loop and all sessions have finished. Only
	/// meaningful after stop().
	[[nodiscard]] auto drained() noexcept {
		return stdexec::starts_on(drain_context_.get_scheduler(),
								  scope_.join());
	}

private:
	boost::asio::io_context &ioc_;
	boost::asio::ip::tcp::acceptor acceptor_;
	Executor &executor_;
	const CommandHandler &handler_;
	CommandContext &ctx_;
	/// Where drained() completes: join needs a scheduler to complete on.
	AsioContext drain_context_;
	stdexec::counting_scope scope_;
};

} // namespace v2
} // namespace redis
