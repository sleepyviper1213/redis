#pragma once

#include "core/stdexec_ext/task.hpp"
#include "net_error.hpp"

#include "detail/asio_context.hpp"
#include "detail/session.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <exec/asio/use_sender.hpp>
#include <stdexec/execution.hpp>

#include <concepts>
#include <expected>
#include <memory>
#include <string_view>

namespace redis {
struct Services;

/**
 * \brief Open \p acceptor on \p host:\p port (IPv4) and start listening.
 *
 * What the acceptor's own constructor does, but a port in use or a host that
 * is not an address comes back as a ListenError instead of an exception.
 */
[[nodiscard]] std::expected<void, ListenError>
listen_on(boost::asio::ip::tcp::acceptor &acceptor, std::string_view host,
		  boost::asio::ip::port_type port);

/**
 * \brief What a BasicListener accepts on.
 *
 * A TCP acceptor (it is opened, bound and closed as one) whose async_accept,
 * given an executor for the new socket and use_sender, gives a sender of the
 * accepted socket. The server uses tcp::acceptor itself; a test derives one
 * whose async_accept fails on demand, to exercise the loop's error path.
 *
 * \tparam A The acceptor's type.
 */
template <class A>
concept Acceptor =
	std::derived_from<A, boost::asio::ip::tcp::acceptor> &&
	std::move_constructible<A> &&
	requires(A &acceptor, boost::asio::any_io_executor ex) {
		{
			acceptor.async_accept(ex, exec::asio::use_sender)
		} -> stdexec::sender;
	};

/**
 * \brief Accepts TCP connections and serves each with a Session.
 *
 * The accept loop is run(), a sender the caller starts and supervises: its
 * failure is the caller's to handle. Every session is a sender started into a
 * counting scope the listener owns: stop() closes the acceptor, which ends
 * run(), and cancels the sessions through the scope's stop token; drained()
 * completes once they have finished unwinding. The listener must be drained
 * before it is destroyed.
 *
 * Safe to run on several I/O threads: the acceptor lives on its own strand
 * and every session gets one of its own.
 *
 * The member functions are defined in detail/listener_impl.tpp. Listener (on
 * tcp::acceptor) is instantiated once, in listener.cpp; a test that injects
 * another acceptor includes that header to instantiate its own.
 *
 * \tparam A The acceptor's type (an Acceptor): tcp::acceptor in the server,
 *         so the accept path costs nothing for being injectable.
 */
template <Acceptor A>
class [[nodiscard]] BasicListener {
public:
	/**
	 * \p services is shared with every session. With \p concurrent false only
	 * one thread runs \p ex, so the per-client strands that serialize
	 * handlers are left out: a strand costs a lock and a queue hop on every
	 * completion. Call listen() before run().
	 */
	BasicListener(boost::asio::any_io_executor ex,
				  std::shared_ptr<Services> services, bool concurrent = true)
		requires std::constructible_from<
			A, boost::asio::strand<boost::asio::any_io_executor>>
		: BasicListener(A(boost::asio::make_strand(ex)), ex,
						std::move(services), concurrent) {}

	/**
	 * \brief As above, accepting on \p acceptor: the injection point for a
	 * test's acceptor. It must not be open yet (listen() opens it) and must
	 * run on a strand of \p ex, where the accept loop runs.
	 */
	BasicListener(A acceptor, boost::asio::any_io_executor ex,
				  std::shared_ptr<Services> services, bool concurrent = true);

	/// Bind and listen on \p host:\p port (IPv4). Fails when the port is in use
	/// or \p host is not an address.
	[[nodiscard]] std::expected<void, ListenError>
	listen(std::string_view host, boost::asio::ip::port_type port);

	/// The port actually bound: what listen() chose when given port 0. Only
	/// meaningful after a successful listen().
	[[nodiscard]] boost::asio::ip::port_type port() const {
		boost::system::error_code ignored;
		return acceptor_.local_endpoint(ignored).port();
	}

	/**
	 * \brief The accept loop: accepts clients and starts a session for each,
	 * until the acceptor is closed (by stop()) or a stop request reaches it.
	 *
	 * A failed accept is logged and tried again after a short pause, so a
	 * transient failure (out of descriptors) does not end it. It completes
	 * with an error only when something unexpected escapes; that is the
	 * caller's to handle (the server treats it as fatal: FailPolicy::FailFast).
	 * Runs on the acceptor's strand once started.
	 */
	stdexec_ext::InlineTask<void> run();

	/// Stop accepting and cancel every session. May be called from any
	/// thread. Follow with drained().
	void stop() noexcept;

	/// Completes once every session has finished. Only meaningful after
	/// stop().
	[[nodiscard]] stdexec::sender auto drained() noexcept {
		return stdexec::starts_on(drain_context_.get_scheduler(),
								  scope_.join());
	}

private:
	boost::asio::any_io_executor ex_;
	A acceptor_;
	std::shared_ptr<Services> services_;
	/// Whether several threads run ex_, and so each client needs a strand.
	bool concurrent_;
	/// Paces the accept loop after a failed accept, so it cannot spin.
	boost::asio::steady_timer retry_timer_;
	/// Where drained() completes: join needs a scheduler to complete on.
	detail::AsioContext drain_context_;
	stdexec::counting_scope scope_;
};

/// The server's listener, on a plain tcp::acceptor.
using Listener = BasicListener<boost::asio::ip::tcp::acceptor>;

// Instantiated once, in listener.cpp.
extern template class BasicListener<boost::asio::ip::tcp::acceptor>;

} // namespace redis
