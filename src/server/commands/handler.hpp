#pragma once
#include "core/resp/value.hpp"
#include "command.hpp"
#include "server/storage/object.hpp"

#include <string>

namespace redis {

/**
 * \brief Dispatches client requests to command implementations.
 *
 * Validates the request shape and arity once, here, so command
 * implementations can index their arguments without re-checking.
 */
class CommandHandler {
public:
	CommandHandler();

	/**
	 * \brief Execute one request (an array of bulk strings) and return the
	 * RESP-encoded reply. Malformed requests yield an error reply, never a
	 * throw.
	 */
	[[nodiscard]] std::string execute(CommandContext &ctx,
									  const resp::Value &request) const;

	/**
	 * \brief Like the above, but appends the reply to \p out.
	 */
	void execute(CommandContext &ctx, const resp::Value &request,
				 std::string &out) const;

	/**
	 * \brief Execute one request given as its arguments, appending the reply
	 * to \p out. The hot path: arguments are views into the client's buffer
	 * and a whole batch of replies builds up in one string, so nothing is
	 * allocated per request. Empty \p args is a malformed request.
	 */
	void execute(CommandContext &ctx, Args args, std::string &out) const;

private:
	StringMap<CommandInfo> commands_;
};
} // namespace redis
