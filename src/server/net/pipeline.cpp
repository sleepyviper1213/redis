#include "pipeline.hpp"

#include "core/resp/request.hpp"
#include "server/commands/handler.hpp"

#include <fmt/format.h>

namespace redis {

void parse_pending(std::string_view pending, ParsedBatch &batch) {
	batch.args.clear();
	batch.ends.clear();
	batch.owned.clear();
	batch.protocol_error.clear();
	size_t consumed = 0;

	while (consumed < pending.size()) {
		size_t pos       = consumed;
		const auto found = resp::parse_request(pending, pos, batch.args,
											   batch.owned);
		if (!found) {
			if (resp::Parser::is_incomplete(found.error())) break;
			batch.protocol_error =
				fmt::format("{:e}",
							reply::error(fmt::format("ERR Protocol error: {}",
													 found.error())));
			break;
		}
		consumed = pos;
		if (*found != resp::RequestKind::Blank)
			batch.ends.push_back(batch.args.size());
	}
	batch.consumed = consumed;
}

AfterReplies execute_batch(const ParsedBatch &batch, std::string &replies,
						   const CommandHandler &handler, CommandContext &ctx) {
	for (size_t i = 0; i < batch.size(); ++i)
		handler.execute(ctx, batch.request(i), replies);

	if (batch.protocol_error.empty()) return AfterReplies::KeepOpen;
	replies += batch.protocol_error;
	return AfterReplies::Close;
}

void drop_consumed(std::string &pending, ParsedBatch &batch) {
	// One erase per batch, not per request: pipelined requests are cut by
	// position and the consumed prefix is dropped once.
	pending.erase(0, batch.consumed);
	batch.consumed = 0;
	batch.args.clear();
	batch.ends.clear();
	batch.owned.clear();
}

AfterReplies execute_pending(std::string &pending, ParsedBatch &batch,
							 std::string &replies,
							 const CommandHandler &handler,
							 CommandContext &ctx) {
	parse_pending(pending, batch);
	const AfterReplies after = execute_batch(batch, replies, handler, ctx);
	drop_consumed(pending, batch);
	return after;
}

} // namespace redis
