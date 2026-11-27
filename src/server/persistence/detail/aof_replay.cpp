#include "aof_replay.hpp"

#include "common/resp/parser.hpp"
#include "common/resp/request.hpp"
#include "core/error/failed.hpp"
#include "server/commands/args.hpp"
#include "server/commands/handler.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace redis::persistence::detail {

std::expected<size_t, PersistError>
replay_log(std::string_view data, CommandContext &ctx,
		   const CommandHandler &handler, size_t &commands) {
	std::vector<std::string_view> args;
	std::deque<std::string> owned;
	std::string reply;
	size_t pos = 0;

	while (pos < data.size()) {
		args.clear();
		owned.clear();
		const auto found = resp::parse_request(data, pos, args, owned);
		if (!found) {
			// parse_request leaves `pos` at the request that failed: the end
			// of the last whole one.
			if (resp::Parser::is_incomplete(found.error())) return pos;
			REDIS_FAILED(
				PersistError{.kind = PersistKind::LogCorrupt,
							 .at   = static_cast<std::uint64_t>(pos),
							 .code = static_cast<std::int64_t>(found.error())});
		}
		if (*found != resp::RequestKind::Command) continue;
		reply.clear();
		handler.execute(ctx, Args{args}, reply);
		++commands;
	}
	return pos;
}

} // namespace redis::persistence::detail
