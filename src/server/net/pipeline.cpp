#include "pipeline.hpp"

#include "core/resp/request.hpp"
#include "server/commands/handler.hpp"

#include <fmt/format.h>

namespace redis {

ParsedBatch parse_pending(std::string &pending) {
	ParsedBatch batch;
	size_t consumed = 0;

	while (consumed < pending.size()) {
		size_t pos   = consumed;
		auto request = resp::parse_request(pending, pos);
		if (!request) {
			if (resp::Parser::is_incomplete(request.error())) break;
			batch.protocol_error =
				fmt::format("{:e}",
							reply::error(fmt::format("ERR Protocol error: {}",
													 request.error())));
			break;
		}
		consumed = pos;
		if (*request) batch.requests.push_back(std::move(**request));
	}

	// One erase per batch, not per request: pipelined requests are cut by
	// position and the consumed prefix is dropped once.
	pending.erase(0, consumed);
	return batch;
}

AfterReplies execute_batch(const ParsedBatch &batch, std::string &replies,
						   const CommandHandler &handler, CommandContext &ctx) {
	for (const auto &request : batch.requests)
		handler.execute(ctx, request, replies);

	if (batch.protocol_error.empty()) return AfterReplies::KeepOpen;
	replies += batch.protocol_error;
	return AfterReplies::Close;
}

AfterReplies execute_pending(std::string &pending, std::string &replies,
							 const CommandHandler &handler,
							 CommandContext &ctx) {
	return execute_batch(parse_pending(pending), replies, handler, ctx);
}

} // namespace redis
