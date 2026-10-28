#pragma once
#include "server/net/nothrow_awaitable_t.hpp" // IWYU pragma: keep

#include "server/commands/command.hpp"
#include "asio_context.hpp"
#include "server/net/pipeline.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <exec/any_sender_of.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <string>

namespace redis {
class CommandHandler;
class Executor;

namespace v2 {

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
 * \brief One client connection, written as a sender chain instead of a
 * coroutine (compare redis::Session).
 *
 * A round is `read | parse | execute | write`, repeated until the client
 * leaves. Each arrow is an Asio operation (through stdexec's use_sender) or a
 * hop between schedulers: reading, parsing and writing stay on the session's
 * strand, and only execution moves to the Executor's thread. The Session
 * object lives in the chain's operation state, so there is no coroutine frame
 * and no shared ownership: whoever starts the sender owns the connection until
 * it completes, and stopping the sender cancels the pending Asio operation.
 */
class Session {
public:
	/**
	 * \brief The sender that serves the client on \p socket. Starting it runs
	 * the session; it completes when the client disconnects or on a protocol
	 * or I/O error (logged), and is stopped when its stop token fires.
	 * \p executor, \p handler and \p ctx must outlive it.
	 */
	[[nodiscard]] static SessionSender
	serve(boost::asio::ip::tcp::socket socket, Executor &executor,
		  const CommandHandler &handler, CommandContext &ctx);

private:
	Session(boost::asio::ip::tcp::socket socket, Executor &executor,
			const CommandHandler &handler, CommandContext &ctx);

	// The stages of one round, each a sender. Defined in session.cpp, their
	// only user, which is why their types can be deduced.
	auto read();
	auto execute();
	auto write();
	/// One round: read, execute, write. Completes with true once the session
	/// is over.
	auto round();

	// What the stages decide with, as plain code.
	/// Take in \p bytes just read. Returns whether there is anything to
	/// execute.
	bool ingest(size_t bytes);
	/// Run everything ingested; the replies end up in replies_.
	void run_batch();
	/// An error ended the round: log it and end the session.
	void fail(std::exception_ptr error);

	static constexpr size_t READ_CHUNK_SIZE = size_t{16} * 1024;

	/// Same limit as redis::Session.
	static constexpr size_t MAX_QUERY_BUFFER = 1024ULL * 1024 * 1024;

	boost::asio::ip::tcp::socket socket_;
	/// The socket's strand as a scheduler, to come back to after executing.
	std::unique_ptr<AsioContext> strand_;
	Executor &executor_;
	const CommandHandler &handler_;
	CommandContext &ctx_;

	std::string chunk_;
	std::string pending_;
	std::string replies_;
	ParsedBatch batch_;
	AfterReplies after_ = AfterReplies::KeepOpen;
	bool done_          = false;
};

} // namespace v2
} // namespace redis
