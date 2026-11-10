// Commands that act on keys regardless of their type: DEL, EXISTS, KEYS,
// TYPE, expiry (EXPIRE family, PERSIST, TTL, PTTL), FLUSHDB, DBSIZE.
#include "command.hpp"
#include "core/utils/parse_number.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep (db.set_expiry, db.expiry, db.size, db.clear)
#include "common/store/object.hpp"
#include "core/chrono/deadline_timer.hpp"
#include "core/utils/glob.hpp"
#include "detail/expire_time.hpp"
#include "server/persistence/append_only_file.hpp"

#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

namespace redis {
using resp::Value;
namespace {
Value del(CommandContext &ctx, Args args) {
	int64_t removed = 0;
	for (const std::string_view key : args.subspan(1))
		removed += ctx.db.erase(key) ? 1 : 0;
	return reply::integer(removed);
}

Value exists(CommandContext &ctx, Args args) {
	int64_t count = 0;
	for (const std::string_view key : args.subspan(1))
		count += ctx.db.contains(key) ? 1 : 0;
	return reply::integer(count);
}

Value keys(CommandContext &ctx, Args args) {
	const std::string_view pattern = args[1];
	// The array header needs the count, so the matches are collected first.
	// The keys live in the table, which nothing changes meanwhile.
	std::vector<std::string_view> matches;
	ctx.db.for_each([&](std::string_view key, auto &&...) {
		if (glob_match(pattern, key)) matches.push_back(key);
	});
	Value::Array found;
	found.reserve(matches.size());
	for (const std::string_view key : matches) found.push_back(reply::bulk(key));
	return reply::array(std::move(found));
}

Value type(CommandContext &ctx, Args args) {
	const Object *object = ctx.db.find(args[1]);
	return reply::simple(
		"{}",
		(object != nullptr) ? std::string_view(type_name(*object)) : "none");
}

// EXPIRE, PEXPIRE, EXPIREAT and PEXPIREAT differ only in how the argument
// becomes a deadline.
template <detail::ExpireUnit Unit, detail::ExpireBase Base>
Value expire_generic(CommandContext &ctx, Args args) {
	const auto amount = parse_integer(args[2]);
	if (!amount) {
		return reply::not_integer();
	}
	const auto deadline = detail::expire_time(*amount, Unit, Base);
	if (!deadline) {
		return reply::error("ERR invalid expire time");
	}
	const DeadlineTimer timer = *deadline;
	const bool applied        = ctx.db.set_expiry(args[1], timer);

	// The relative forms (EXPIRE, PEXPIRE) mean "from now", which a replay
	// does not share: log the absolute deadline. One already past deleted the
	// key.
	if (applied && ctx.aof != nullptr) {
		if (timer.has_expired()) {
			ctx.aof->replace({"DEL", args[1]});
		} else {
			const auto at = timer.millis_since_epoch();
			ctx.aof->replace({"PEXPIREAT", args[1], fmt::to_string(at)});
		}
	}
	return reply::integer(applied ? 1 : 0);
}

Value persist(CommandContext &ctx, Args args) {
	const std::string_view key = args[1];
	return reply::integer(ctx.db.contains(key) && ctx.db.persist(key) ? 1 : 0);
}

// TTL and PTTL reply -2 for a missing key and -1 for a key without expiry,
// as integers, so clients can tell the cases apart.
template <bool InMillis>
Value ttl_generic(CommandContext &ctx, Args args) {
	const std::string_view key = args[1];
	if (!ctx.db.contains(key)) {
		return reply::integer(-2);
	}
	const auto deadline = ctx.db.expiry(key);
	if (!deadline) {
		return reply::integer(-1);
	}

	const auto left = DeadlineTimer::at(*deadline).remaining_time_ms().count();
	// Round to the nearest second like Redis; truncating would report 99
	// immediately after SET ... EX 100.
	return reply::integer(InMillis ? left : (left + 500) / 1000);
}

Value flushdb(CommandContext &ctx, Args /*unused*/) {
	ctx.db.clear();
	return reply::ok();
}

Value dbsize(CommandContext &ctx, Args /*unused*/) {
	return reply::integer(static_cast<int64_t>(ctx.db.size()));
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "DEL", .fn = del, .arity = -2, .write = true},
	CommandInfo{.name = "EXISTS", .fn = exists, .arity = -2},
	CommandInfo{.name = "KEYS", .fn = keys, .arity = 2},
	CommandInfo{.name = "TYPE", .fn = type, .arity = 2},
	CommandInfo{.name  = "EXPIRE",
				.fn    = expire_generic<detail::ExpireUnit::Seconds,
										detail::ExpireBase::Relative>,
				.arity = 3,
				.write = true},
	CommandInfo{.name  = "PEXPIRE",
				.fn    = expire_generic<detail::ExpireUnit::Milliseconds,
										detail::ExpireBase::Relative>,
				.arity = 3,
				.write = true},
	CommandInfo{.name  = "EXPIREAT",
				.fn    = expire_generic<detail::ExpireUnit::Seconds,
										detail::ExpireBase::Absolute>,
				.arity = 3,
				.write = true},
	CommandInfo{.name  = "PEXPIREAT",
				.fn    = expire_generic<detail::ExpireUnit::Milliseconds,
										detail::ExpireBase::Absolute>,
				.arity = 3,
				.write = true},
	CommandInfo{.name = "PERSIST", .fn = persist, .arity = 2, .write = true},
	CommandInfo{.name = "TTL", .fn = ttl_generic<false>, .arity = 2},
	CommandInfo{.name = "PTTL", .fn = ttl_generic<true>, .arity = 2},
	CommandInfo{.name = "FLUSHDB", .fn = flushdb, .arity = -1, .write = true},
	CommandInfo{.name = "DBSIZE", .fn = dbsize, .arity = 1},
};
} // namespace

std::span<const CommandInfo> keyspace_commands() { return COMMANDS; }

} // namespace redis
