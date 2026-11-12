#pragma once
#include "asio_context.hpp"
#include "server/net/guard.hpp"
#include "server/net/net_error.hpp"
#include "server/net/detail/pipeline.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <string>

namespace redis {
struct Services;

namespace detail {

/**
 * What serving a client looks like from outside: it finishes, or is stopped.
 * Type-erased once per connection, which keeps the chain's types out of the
 * header; the stop token is forwarded so cancellation reaches the Asio
 * operation inside.
 */
using SessionSender = exec::any_sender<exec::any_receiver<
	stdexec::completion_signatures<stdexec::set_value_t(),
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
	// only user, which is why their types can be deduced.
	/// Waits for the client's next bytes, or for the idle limit. Completes with
	/// the byte count, or with nothing once the session is over.
	stdexec::sender auto read();
	/// Runs the ingested requests, on the keyspace thread when there is one.
	stdexec::sender auto execute();
	/// Sends replies_ to the client, then clears it.
	stdexec::sender auto write();
	/// One round: read, execute, write. Completes with true once the session
	/// is over.
	stdexec::sender auto round();

	// What the stages decide with, as plain code.
	/// Take in \p bytes just read. Returns whether there is anything to
	/// execute.
	bool ingest(size_t bytes);
	/// Run everything ingested; the replies end up in replies_.
	void run_batch();
	/// An error no stage claimed ended the round: log it and end the session.
	void fail(const std::exception_ptr &error);
	/// The stage \p kind failed with \p error: log it under that kind and end
	/// the session. A client leaving (end of file) is not a failure.
	void fail_io(ConnectionKind kind, const std::exception_ptr &error);

	static constexpr size_t READ_CHUNK_SIZE = 16uz * 1024;

	boost::asio::ip::tcp::socket socket_;
	/// The socket's strand as a scheduler, to come back to after executing.
	std::unique_ptr<AsioContext> strand_;
	/// Races each read when the guard has an idle limit; the loser is cancelled.
	boost::asio::steady_timer idle_timer_;
	std::shared_ptr<Services> services_;
	/// Holds this client's slot under Services::guard until the session ends.
	NetGuard::Ticket ticket_;
	/// "address:port", for the log lines about this client.
	std::string peer_;

	std::string chunk_;
	std::string pending_;
	std::string replies_;
	/// How much of replies_ the socket took at once; see write().
	size_t sent_ = 0;
	ParsedBatch batch_;
	AfterReplies after_ = AfterReplies::KeepOpen;
	bool done_          = false;
};

} // namespace detail
} // namespace redis
