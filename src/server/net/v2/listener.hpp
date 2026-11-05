#pragma once

#include "server/net/nothrow_awaitable_t.hpp" // IWYU pragma: keep

#include "asio_context.hpp"
#include "session.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <stdexec/execution.hpp>

#include <expected>
#include <memory>
#include <string>

namespace redis {
struct Services;

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
class [[nodiscard]] Listener {
public:
	/// \p services is shared with every session. Call listen() before start().
	Listener(boost::asio::any_io_executor ex,
			  std::shared_ptr<Services> services);

	/// Bind and listen on \p host:\p port (IPv4). Fails, with a message, when
	/// the port is in use or \p host is not an address.
	[[nodiscard]] std::expected<void, std::string>
	listen(const std::string &host, boost::asio::ip::port_type port);

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
	boost::asio::any_io_executor ex_;
	boost::asio::ip::tcp::acceptor acceptor_;
	std::shared_ptr<Services> services_;
	/// Paces the accept loop after a failed accept, so it cannot spin.
	boost::asio::steady_timer retry_timer_;
	/// Where drained() completes: join needs a scheduler to complete on.
	AsioContext drain_context_;
	stdexec::counting_scope scope_;
};

} // namespace v2
} // namespace redis
