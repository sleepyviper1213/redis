#include "server/net/pipeline.hpp"

#include "core/resp/request.hpp"
#include "server/commands/handler.hpp"

#include <fmt/format.h>

namespace redis {

AfterReplies execute_pending(std::string &pending, std::string &replies,
							 const CommandHandler &handler,
							 CommandContext &ctx) {
	size_t consumed    = 0;
	AfterReplies after = AfterReplies::KeepOpen;

	while (consumed < pending.size()) {
		size_t pos         = consumed;
		const auto request = resp::parse_request(pending, pos);
		if (!request) {
			if (resp::Parser::is_incomplete(request.error())) break;
			replies += fmt::format(
				"{:e}",
				reply::error(
					fmt::format("ERR Protocol error: {}", request.error())));
			after = AfterReplies::Close;
			break;
		}
		consumed = pos;
		if (*request) replies += handler.execute(ctx, **request);
	}

	// One erase per batch, not per request: pipelined requests are cut by
	// position and the consumed prefix is dropped once.
	pending.erase(0, consumed);
	return after;
}

} // namespace redis
