#include "pipeline.hpp"

#include "core/resp/parser.hpp"
#include "core/resp/request.hpp"
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep (db.size(), db.prefetch())
#include "server/storage/database_fwd.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <string_view>

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
			ReplyWriter(batch.protocol_error)
				.error(fmt::format("ERR Protocol error: {}", found.error()));
			break;
		}
		consumed = pos;
		if (*found != resp::RequestKind::Blank)
			batch.ends.push_back(batch.args.size());
	}
	batch.consumed = consumed;
}

namespace {
// Below this many keys the table fits in the CPU caches and a lookup does not
// miss, so prefetching would only hash every key twice.
constexpr size_t PREFETCH_MIN_KEYS = size_t{1} << 16;

// Start loading the table memory of every request's first argument (the key,
// for the commands that take one) before running any of them, so the cache
// misses of a pipelined batch overlap instead of happening one at a time.
void prefetch_keys(const ParsedBatch &batch, const Database &db) {
	if (batch.size() < 2 || db.size() < PREFETCH_MIN_KEYS) return;
	for (size_t i = 0; i < batch.size(); ++i) {
		const Args request = batch.request(i);
		if (request.size() >= 2) db.prefetch(request[1]);
	}
}
} // namespace

AfterReplies execute_batch(const ParsedBatch &batch, std::string &replies,
						   const CommandHandler &handler, CommandContext &ctx) {
	prefetch_keys(batch, ctx.db);
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
