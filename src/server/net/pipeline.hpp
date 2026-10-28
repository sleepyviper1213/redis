#pragma once
#include "server/commands/command.hpp"

#include <string>

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
 * \brief Execute every complete request buffered in \p pending.
 *
 * Cuts requests (RESP arrays or inline commands) off the front of \p pending,
 * executes each one and appends its encoded reply to \p replies. Consumed
 * bytes are erased from \p pending; a trailing partial request stays there
 * until more bytes arrive.
 *
 * This is the server's hot path, kept out of the session coroutine on
 * purpose: it is plain synchronous code, so its locals live in registers and
 * on the stack rather than in the heap-allocated coroutine frame, and the
 * compiler optimises it as an ordinary function. The coroutine is left with
 * only the I/O.
 */
AfterReplies execute_pending(std::string &pending, std::string &replies,
							 const CommandHandler &handler,
							 CommandContext &ctx);

} // namespace redis
