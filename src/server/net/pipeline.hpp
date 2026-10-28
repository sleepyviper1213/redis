#pragma once
#include "core/resp/value.hpp"
#include "server/commands/command.hpp"

#include <string>
#include <vector>

namespace redis {
class CommandHandler;

/// What the connection should do once the replies have been written.
enum class [[nodiscard]] AfterReplies {
	KeepOpen,
	/// A protocol error was answered; the stream position can no longer be
	/// trusted, so the connection must close (as Redis does).
	Close,
};

/// The complete requests cut off the front of a client's buffer.
struct [[nodiscard]] ParsedBatch {
	std::vector<resp::Value> requests;
	/// Set when the bytes after \c requests are not a valid request: the
	/// RESP-encoded error to send once the requests above have been answered.
	std::string protocol_error;
};

/**
 * \brief Stage one of the hot path: cut every complete request off the front
 * of \p pending.
 *
 * Requests are RESP arrays (client libraries) or inline commands (telnet,
 * redis-benchmark). Consumed bytes are erased from \p pending in one go; a
 * trailing partial request stays there until more bytes arrive.
 *
 * Touches only the client's own buffer, so any number of sessions can run it
 * at once on their I/O threads.
 */
ParsedBatch parse_pending(std::string &pending);

/**
 * \brief Stage two of the hot path: execute \p batch in order and append each
 * encoded reply to \p replies, followed by the batch's protocol error, if any.
 *
 * Touches the keyspace, so it must only run on the one thread that owns it.
 */
AfterReplies execute_batch(const ParsedBatch &batch, std::string &replies,
						   const CommandHandler &handler, CommandContext &ctx);

/**
 * \brief Both stages back to back on the calling thread: the whole hot path as
 * one plain function.
 *
 * This is what a single-threaded server runs, with no hand-off between the
 * stages. Like each stage on its own it is ordinary synchronous code, kept out
 * of the session coroutine on purpose so its locals live in registers and on
 * the stack rather than in the heap-allocated coroutine frame.
 */
AfterReplies execute_pending(std::string &pending, std::string &replies,
							 const CommandHandler &handler,
							 CommandContext &ctx);

} // namespace redis
