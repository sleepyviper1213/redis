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
	 * \brief Like the above, but appends the reply to \p out. The hot path
	 * uses this to build a whole batch of replies in one buffer, without a
	 * temporary string per request.
	 */
	void execute(CommandContext &ctx, const resp::Value &request,
				 std::string &out) const;

private:
	StringMap<CommandInfo> commands_;
};
} // namespace redis
