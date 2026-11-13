#pragma once
#include "asio_context.hpp"
#include "server/net/detail/pipeline.hpp"
#include "server/net/guard.hpp"
#include "server/net/net_error.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
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
using SessionSender = exec::any_sender<
	exec::any_receiver<stdexec::completion_signatures<stdexec::set_value_t(),
													  stdexec::set_stopped_t()>,
					   exec::queries<stdexec::inplace_stop_token(
						   stdexec::get_stop_token_t) noexcept>>>;

/**
 * \brief Closes a socket once a stop is requested, so that every operation
 * pending on it completes.
 *
 * A stop request alone asks Asio to cancel them, and Windows does not complete
 * a cancelled send to a peer that is not reading: draining the sessions at
 * shutdown would wait for it forever. On a closed socket every pending
 * operation completes. The close is posted to the socket's executor (its
 * strand, when it has one), so it never overlaps the session's own handlers,
 * and it does nothing once the session has ended.
 */
class CloseOnStop {
public:
	CloseOnStop()                               = default;
	CloseOnStop(CloseOnStop &&) noexcept        = default;
	CloseOnStop(const CloseOnStop &)            = delete;
	CloseOnStop &operator=(const CloseOnStop &) = delete;
	CloseOnStop &operator=(CloseOnStop &&)      = delete;
	/// Forgets the socket: a close still queued finds nothing to close.
	~CloseOnStop();

	/// From now on, a stop request on \p stop closes \p socket, which must
	/// not move any more. Called once.
	void arm(boost::asio::ip::tcp::socket &socket,
			 stdexec::inplace_stop_token stop);

private:
	/// What the queued close and the destructor share: the socket, until
	/// the session ends.
	struct Target {
		std::mutex mutex;
		boost::asio::ip::tcp::socket *socket = nullptr;
	};

	/// Runs on the socket's executor.
	static void close(Target &target);

	/// The stop callback: queues the close.
	struct Request {
		std::shared_ptr<Target> target;
		boost::asio::any_io_executor executor;
		void operator()() const noexcept;
	};

	std::shared_ptr<Target> target_;
	std::unique_ptr<stdexec::inplace_stop_callback<Request>> callback_;
};

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
	/**
	 * Answers what was ingested: execute(), then write(), within the request
	 * limit when the guard has one. Past it the replies are dropped and the
	 * session ends.
	 */
	stdexec::sender auto respond();
	/// One round: read, then respond. Completes with true once the session is
	/// over.
	stdexec::sender auto round();
	/// Every round, until the session is over; a stop request closes the
	/// socket (see CloseOnStop).
	stdexec::sender auto run();

	// What the stages decide with, as plain code.
	/// Take in \p bytes just read. Returns whether there is anything to
	/// execute.
	bool ingest(size_t bytes);
	/// Run everything ingested; the replies end up in replies_.
	void run_batch();
	/// The stage \p kind failed with \p error: log it under that kind and end
	/// the session. A client leaving (end of file) is not a failure, nor is
	/// an operation cut short by the server closing the socket itself.
	void fail_io(ConnectionKind kind, const std::exception_ptr &error);

	static constexpr size_t READ_CHUNK_SIZE = 16uz * 1024;

	boost::asio::ip::tcp::socket socket_;
	/// After socket_, so that it lets go of the socket first.
	CloseOnStop close_on_stop_;
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
