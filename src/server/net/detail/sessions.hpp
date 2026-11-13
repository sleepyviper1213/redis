#pragma once

#include "asio_context.hpp"
#include "session.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <stdexec/execution.hpp>

#include <memory>
#include <utility>

namespace redis {
struct Services;

namespace detail {

/**
 * \brief The running sessions: starts one per connection, cancels them all,
 * and says when they have finished.
 *
 * Each session is a sender started into a counting scope, which owns it until
 * it completes; stop() cancels every one through the scope's stop token.
 * Must be drained (stop(), then drained()) before it is destroyed. The
 * listener serves each accepted client through one; a test serves the
 * connection it opened itself.
 */
class Sessions {
public:
	/// \p ex runs drained()'s completion: joining a scope needs a scheduler.
	explicit Sessions(boost::asio::any_io_executor ex)
		: drain_context_(std::move(ex)) {}

	/// Serves the client on \p socket with \p services until the client
	/// leaves, the session fails (logged) or stop() is called.
	void serve(boost::asio::ip::tcp::socket socket,
			   std::shared_ptr<Services> services) {
		stdexec::spawn(Session::serve(std::move(socket), std::move(services)),
					   scope_.get_token());
	}

	/// Cancels every session, each of which also closes its socket, so that
	/// no pending operation can keep drained() waiting (see CloseOnStop). May
	/// be called from any thread, more than once.
	void stop() noexcept { scope_.request_stop(); }

	/// Completes once every session has finished. Only meaningful after
	/// stop(), or once every client has left.
	[[nodiscard]] stdexec::sender auto drained() noexcept {
		return stdexec::starts_on(drain_context_.get_scheduler(),
								  scope_.join());
	}

private:
	AsioContext drain_context_;
	stdexec::counting_scope scope_;
};

} // namespace detail
} // namespace redis
