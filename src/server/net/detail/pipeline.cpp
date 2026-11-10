#include "pipeline.hpp"

#include "common/resp/parser.hpp"
#include "common/resp/request.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep (db.size(), db.prefetch())
#include "common/store/database_fwd.hpp"
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/net/net_error.hpp"
#include "server/persistence/append_only_file.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <iterator>
#include <string_view>

namespace redis::detail {

void parse_pending(std::string_view pending, ParsedBatch &batch) {
	batch.args.clear();
	batch.ends.clear();
	batch.owned.clear();
	batch.protocol_error.clear();
	size_t consumed = 0;

	while (consumed < pending.size()) {
		size_t pos = consumed;
		const auto found =
			resp::parse_request(pending, pos, batch.args, batch.owned);
		if (!found) {
			if (resp::Parser::is_incomplete(found.error())) break;
			// The wording is ConnectionError's; the "ERR" is the protocol's.
			fmt::format_to(
				std::back_inserter(batch.protocol_error),
				"{:e}",
				resp::Value::from_simple_error(fmt::format(
					"ERR {}",
					ConnectionError{
						.kind = ConnectionKind::Protocol,
						.code = static_cast<std::int64_t>(found.error())})));
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
						   const CommandHandler &handler, CommandContext &ctx,
						   size_t max_output) {
	prefetch_keys(batch, ctx.db);
	bool over_limit = false;
	for (size_t i = 0; i < batch.size() && !over_limit; ++i) {
		handler.execute(ctx, batch.request(i), replies);
		over_limit = replies.size() > max_output;
	}
	// The replies go out once this returns: what they acknowledge must be in
	// the file by then (and on disk, with appendfsync always).
	if (ctx.aof != nullptr) ctx.aof->after_batch();

	if (over_limit) return AfterReplies::Close;
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
							 const CommandHandler &handler, CommandContext &ctx,
							 size_t max_output) {
	parse_pending(pending, batch);
	const AfterReplies after =
		execute_batch(batch, replies, handler, ctx, max_output);
	drop_consumed(pending, batch);
	return after;
}

} // namespace redis::detail
