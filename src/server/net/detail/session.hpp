#pragma once
#include "asio_context.hpp"
#include "close_socket_on_stop.hpp"
#include "conversation.hpp"
#include "core/stdexec_ext/task.hpp"
#include "server/net/guard.hpp"
#include "server/net/net_error.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>

namespace redis {
struct Services;

namespace detail {

/**
 * What serving a client looks like from outside: it finishes, or is stopped.
 * Type-erased once per connection, which keeps the chain's types out of the
 * header; the stop token is forwarded so cancellation reaches the Asio
 * operation inside.
 */
using SessionSender = exec::any_sender<
	exec::any_receiver<stdexec::completion_signatures<stdexec::set_value_t(),
													  stdexec::set_stopped_t()>,
					   exec::queries<stdexec::inplace_stop_token(
						   stdexec::get_stop_token_t) noexcept>>>;

/**
 * \brief One client connection, written as a sender chain.
 *
 * A round is `read | parse | execute | write`, repeated until the client
 * leaves. Each arrow is an Asio operation (through stdexec's use_sender) or a
 * hop between schedulers: reading, parsing and writing stay on the session's
 * strand, and only execution moves to the Executor's thread. The Session
 * object lives in the chain's operation state, so there is no coroutine frame
 * and no shared ownership of the connection: whoever starts the sender owns it
 * until it completes, and stopping the sender cancels the pending Asio
 * operation. Only the Services are shared, one set for all sessions.
 */
class Session {
public:
	/**
	 * \brief The sender that serves the client on \p socket. Starting it runs
	 * the session; it completes when the client disconnects or on a protocol
	 * or I/O error (logged), and is stopped when its stop token fires.
	 */
	[[nodiscard]] static SessionSender
	serve(boost::asio::ip::tcp::socket socket,
		  std::shared_ptr<Services> services);

private:
	Session(boost::asio::ip::tcp::socket socket,
			std::shared_ptr<Services> services, NetGuard::Ticket ticket);

	// The stages of one round, each a sender. Defined in session.cpp, their
	// only user, which is why their types can be deduced. run() is a coroutine,
	// which is where the chain of a whole session is cut: too deep otherwise
	// for stdexec to compute its completion signatures in reasonable time and
	// memory.
	/// Waits for the client's next bytes, or for the idle limit. Completes with
	/// the byte count, or with nothing once the session is over.
	stdexec::sender auto read();
	/// Runs the ingested requests, on the keyspace thread when there is one.
	stdexec::sender auto execute();
	/// Sends the conversation's replies to the client.
	stdexec::sender auto write();
	/**
	 * Answers what was ingested: execute(), then write(), within the request
	 * limit when the guard has one. Past it the replies are dropped and the
	 * session ends.
	 */
	stdexec::sender auto respond();
	/// One round: read, then respond. Completes with true once the session is
	/// over.
	stdexec::sender auto round_chain();
	/// Every round, until the session is over; a stop request closes the
	/// socket (see CloseSocketOnStop).
	stdexec_ext::InlineTask<void> run();

	/// The stage \p kind failed with \p error: log it under that kind (see
	/// report_for(): a client leaving is not a failure, nor is an operation cut
	/// short by the server closing the socket itself) and end the session.
	void fail_io(ConnectionKind kind, const std::exception_ptr &error);
	/// What the conversation owes the client, as a buffer to send.
	[[nodiscard]] boost::asio::const_buffer replies_buffer() const noexcept;

	/// How the wait for a request ends. Plain data: it is built by the stages
	/// of the race, which therefore do nothing but report.
	struct Outcome {
		std::optional<size_t> bytes; ///< Nothing once the session is over.
		bool idle = false;           ///< The idle timer won.
	};
	/// Reads what the socket has. Readiness can be spurious: that is a round
	/// that read nothing, which ingests nothing and goes back to waiting.
	/// Throws the socket's error.
	[[nodiscard]] Outcome read_available();
	/**
	 * The request limit ran out: count it and close the socket.
	 *
	 * What a timeout cuts short depends on where it finds the batch, which is
	 * the delivery guarantee: one still queued for the keyspace thread never
	 * runs (the pool checks the stop request before running it), one already
	 * running completes, and a write in progress is abandoned. Either way the
	 * replies are dropped with the client.
	 *
	 * The socket is closed rather than only cancelled: a stop request asks
	 * Asio to cancel the send, and Windows does not complete a send the peer
	 * is not reading, so the race would wait for it forever. On a closed
	 * socket every pending operation completes. This runs on the session's
	 * strand, so it cannot overlap the write's own handlers, and the session
	 * is over anyway.
	 *
	 * Counted here, when the limit runs out, not once the race is over: that
	 * waits for the abandoned work to unwind, which for a batch queued behind a
	 * long one is as late as that one finishes.
	 */
	void give_up_waiting_for_response(std::chrono::seconds limit);

	boost::asio::ip::tcp::socket socket_;
	/// After socket_, so that it lets go of the socket first.
	CloseSocketOnStop close_on_stop_;
	/// The socket's strand as a scheduler, to come back to after executing.
	std::unique_ptr<AsioContext> strand_;
	/// Times each read's time limit when the guard has an idle limit, as a
	/// TimerScheduler: one timer for every round, not one per round.
	boost::asio::steady_timer idle_timer_;
	/// Times each response's time limit (execute and write) when the guard has
	/// a request limit. Not idle_timer_: a TimerScheduler takes one wait at a
	/// time, and one timer per limit keeps each race readable.
	boost::asio::steady_timer request_timer_;
	std::shared_ptr<Services> services_;
	/// Holds this client's slot under Services::guard until the session ends.
	NetGuard::Ticket ticket_;
	/// What this client sent, what it is owed, and whether it is over: every
	/// decision of the session, apart from its I/O (ADR-0012).
	Conversation conversation_;
	/// How much of the conversation's replies the socket took at once; see
	/// write().
	size_t sent_ = 0;
};

} // namespace detail
} // namespace redis
