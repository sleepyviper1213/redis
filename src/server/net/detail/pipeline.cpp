#include "pipeline.hpp"

#include "common/resp/parser.hpp"
#include "common/resp/request.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep (db.size(), db.prefetch())
#include "common/store/database_fwd.hpp"
#include "core/utils/attributes.hpp"
#include "core/utils/realtime.hpp"
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/net/net_error.hpp"
#include "server/persistence/append_only_file.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace redis::detail {

namespace {
// Most entries reserved ahead of a parse: a client cannot make the session
// hold more memory than this by sending bare newlines. A batch that needs more
// grows as it parses, which is correct, only not free.
constexpr size_t MAX_BATCH_RESERVE = 1z << 14;

// Makes room for what \p pending can hold, so cutting it into requests
// allocates nothing in the usual case. Every RESP argument takes two lines and
// every request at least one, so the line count bounds both vectors; only an
// inline command with many words on one line can need more.
void reserve_batch(std::string_view pending, ParsedBatch &batch) {
	const auto lines  = static_cast<size_t>(std::ranges::count(pending, '\n'));
	const size_t want = std::min(lines, MAX_BATCH_RESERVE);
	const auto grow   = [want](auto &vec) {
		if (vec.capacity() < want)
			vec.reserve(std::max(want, vec.capacity() * 2));
	};
	grow(batch.args);
	grow(batch.ends);
}

// The cutting itself. It runs in a real-time context: under RealtimeSanitizer
// anything here that allocates, frees or blocks aborts, unless it sits inside
// a RealtimeExemption (the generic parser, a vector that outgrew its reserve).
// clang's static check cannot see through the std::expected and vector
// internals below, so it is told to trust this function and the sanitizer
// vouches for it instead.
//
// Returns the error that ended the stream, if one did. Its reply is built by
// the caller: formatting allocates, and ConnectionError carries members clang
// cannot prove nonblocking.
REDIS_TRUSTED_NONBLOCKING_BEGIN
std::optional<resp::Parser::RespError>
cut_requests(std::string_view pending, ParsedBatch &batch) REDIS_NONBLOCKING {
	size_t consumed = 0;
	std::optional<resp::Parser::RespError> broken;

	while (consumed < pending.size()) {
		size_t pos = consumed;
		const auto found =
			resp::parse_request(pending, pos, batch.args, batch.owned);
		if (!found) {
			if (!resp::Parser::is_incomplete(found.error()))
				broken = found.error();
			break;
		}
		consumed = pos;
		if (*found != resp::RequestKind::Blank)
			push_back_growing(batch.ends, batch.args.size());
	}
	batch.consumed = consumed;
	return broken;
}

REDIS_TRUSTED_NONBLOCKING_END

// A broken stream ends the connection after one error reply.
void write_protocol_error(resp::Parser::RespError error, std::string &out) {
	// The wording is ConnectionError's; the "ERR" is the protocol's.
	fmt::format_to(
		std::back_inserter(out),
		"{:e}",
		resp::Value::from_simple_error(fmt::format(
			"ERR {}",
			ConnectionError{.kind = ConnectionKind::Protocol,
							.code = static_cast<std::int64_t>(error)})));
}
} // namespace

void parse_pending(std::string_view pending, ParsedBatch &batch) {
	// Clearing frees the strings the previous batch decoded, and reserving may
	// allocate: both stay outside the real-time part, as does the error reply.
	batch.args.clear();
	batch.ends.clear();
	batch.owned.clear();
	batch.protocol_error.clear();
	reserve_batch(pending, batch);
	if (const auto broken = cut_requests(pending, batch))
		write_protocol_error(*broken, batch.protocol_error);
}

namespace {
// Below this many keys the table fits in the CPU caches and a lookup does not
// miss, so prefetching would only hash every key twice.
constexpr size_t PREFETCH_MIN_KEYS = 1z << 16;

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
