#pragma once
#include "core/resp/value.hpp"
#include "server/commands/command.hpp"

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
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

/**
 * \brief The complete requests cut off the front of a client's buffer.
 *
 * Arguments are views into that buffer, not copies, so parsing allocates
 * nothing per request. The buffer must therefore stay untouched from
 * parse_pending() until drop_consumed(). A session keeps one batch for its
 * whole life, so the vectors below reach their working size once and are
 * then reused.
 */
struct ParsedBatch {
	/// Every argument of every request, back to back.
	std::vector<std::string_view> args;
	/// Where each request's arguments end in \c args. An empty request is a
	/// malformed one, which the handler answers with an error.
	std::vector<size_t> ends;
	/// Arguments the generic parser had to decode; see resp::parse_request().
	std::deque<std::string> owned;
	/// Bytes of the client's buffer that the requests above were cut from.
	size_t consumed = 0;
	/// Set when the bytes after the requests are not a valid request: the
	/// RESP-encoded error to send once the requests have been answered.
	std::string protocol_error;

	/// Number of requests.
	[[nodiscard]] size_t size() const noexcept { return ends.size(); }

	/// The arguments of request \p i.
	[[nodiscard]] Args request(size_t i) const noexcept {
		const size_t first = i == 0 ? 0 : ends[i - 1];
		return Args(args).subspan(first, ends[i] - first);
	}
};

/**
 * \brief Stage one of the hot path: cut every complete request off the front
 * of \p pending into \p batch, replacing what it held.
 *
 * Requests are RESP arrays (client libraries) or inline commands (telnet,
 * redis-benchmark). Nothing is erased yet: the requests view \p pending, so
 * the consumed bytes are dropped by drop_consumed() once they have run. A
 * trailing partial request stays until more bytes arrive.
 *
 * Touches only the client's own buffer, so any number of sessions can run it
 * at once on their I/O threads.
 */
void parse_pending(std::string_view pending, ParsedBatch &batch);

/**
 * \brief Stage two of the hot path: execute \p batch in order and append each
 * encoded reply to \p replies, followed by the batch's protocol error, if any.
 *
 * Touches the keyspace, so it must only run on the one thread that owns it.
 */
AfterReplies execute_batch(const ParsedBatch &batch, std::string &replies,
						   const CommandHandler &handler, CommandContext &ctx);

/**
 * \brief Erase the bytes \p batch was cut from off the front of \p pending, in
 * one go, and empty \p batch. Call it after execute_batch() and before
 * appending to \p pending again; until then the requests view those bytes.
 */
void drop_consumed(std::string &pending, ParsedBatch &batch);

/**
 * \brief The three steps above back to back on the calling thread: the whole
 * hot path as one plain function. \p batch is only scratch space, passed in so
 * its capacity is reused; it is empty on return.
 *
 * This is what a single-threaded server runs, with no hand-off between the
 * stages. Like each stage on its own it is ordinary synchronous code, kept out
 * of the session coroutine on purpose so its locals live in registers and on
 * the stack rather than in the heap-allocated coroutine frame.
 */
AfterReplies execute_pending(std::string &pending, ParsedBatch &batch,
							 std::string &replies,
							 const CommandHandler &handler,
							 CommandContext &ctx);

} // namespace redis
