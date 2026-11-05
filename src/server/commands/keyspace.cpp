// Commands that act on keys regardless of their type: DEL, EXISTS, KEYS,
// TYPE, expiry (EXPIRE family, PERSIST, TTL, PTTL), FLUSHDB, DBSIZE.
#include "command.hpp"
#include "core/utils/glob.hpp"
#include "server/commands/reply.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep
#include "server/storage/object.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace redis {

namespace {
using std::chrono::milliseconds;
using std::chrono::seconds;

void del(CommandContext &ctx, Args args, ReplyWriter &out) {
	int64_t removed = 0;
	for (const std::string_view key : args.subspan(1))
		removed += ctx.db.erase(key) ? 1 : 0;
	out.integer(removed);
}

void exists(CommandContext &ctx, Args args, ReplyWriter &out) {
	int64_t count = 0;
	for (const std::string_view key : args.subspan(1))
		count += ctx.db.contains(key) ? 1 : 0;
	out.integer(count);
}

void keys(CommandContext &ctx, Args args, ReplyWriter &out) {
	const std::string_view pattern = args[1];
	// The array header needs the count, so the matches are collected first.
	// The keys live in the table, which nothing changes meanwhile.
	std::vector<std::string_view> matches;
	ctx.db.for_each([&](std::string_view key, auto &&...) {
		if (glob_match(pattern, key)) matches.push_back(key);
	});
	out.array(matches.size());
	for (const std::string_view key : matches) out.bulk(key);
}

void type(CommandContext &ctx, Args args, ReplyWriter &out) {
	const Object *object = ctx.db.find(args[1]);
	out.simple((object != nullptr) ? std::string_view(type_name(*object))
								   : "none");
}

// EXPIRE, PEXPIRE, EXPIREAT and PEXPIREAT differ only in how the argument
// becomes a deadline.
template <auto ToDeadline>
void expire_generic(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto amount = parse_int(args[2]);
	if (!amount) {
		out.not_integer();
		return;
	}
	const auto deadline = ToDeadline(*amount);
	const bool applied  = ctx.db.set_expiry(args[1], deadline);
	out.integer(applied ? 1 : 0);

	// The relative forms (EXPIRE, PEXPIRE) mean "from now", which a replay
	// does not share: log the deadline. One already past deleted the key.
	if (applied && ctx.aof != nullptr) {
		if (deadline <= Database::clock::now()) {
			ctx.aof->replace({"DEL", args[1]});
		} else {
			const auto at = std::chrono::duration_cast<milliseconds>(
								deadline.time_since_epoch())
								.count();
			ctx.aof->replace({"PEXPIREAT", args[1], std::to_string(at)});
		}
	}
}

constexpr auto after_seconds = [](int64_t n) {
	return Database::clock::now() + seconds(n);
};
constexpr auto after_millis = [](int64_t n) {
	return Database::clock::now() + milliseconds(n);
};
constexpr auto at_seconds = [](int64_t n) {
	return Database::time_point(seconds(n));
};
constexpr auto at_millis = [](int64_t n) {
	return Database::time_point(milliseconds(n));
};

void persist(CommandContext &ctx, Args args, ReplyWriter &out) {
	const std::string_view key = args[1];
	out.integer(ctx.db.contains(key) && ctx.db.persist(key) ? 1 : 0);
}

// TTL and PTTL reply -2 for a missing key and -1 for a key without expiry,
// as integers, so clients can tell the cases apart.
template <bool InMillis>
void ttl_generic(CommandContext &ctx, Args args, ReplyWriter &out) {
	const std::string_view key = args[1];
	if (!ctx.db.contains(key)) {
		out.integer(-2);
		return;
	}
	const auto deadline = ctx.db.expiry(key);
	if (!deadline) {
		out.integer(-1);
		return;
	}

	const auto left = std::chrono::duration_cast<milliseconds>(
						  *deadline - Database::clock::now())
						  .count();
	// Round to the nearest second like Redis; truncating would report 99
	// immediately after SET ... EX 100.
	out.integer(InMillis ? left : (left + 500) / 1000);
}

void flushdb(CommandContext &ctx, Args /*unused*/, ReplyWriter &out) {
	ctx.db.clear();
	out.ok();
}

void dbsize(CommandContext &ctx, Args /*unused*/, ReplyWriter &out) {
	out.integer(static_cast<int64_t>(ctx.db.size()));
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "DEL", .fn = del, .arity = -2, .write = true},
	CommandInfo{.name = "EXISTS", .fn = exists, .arity = -2},
	CommandInfo{.name = "KEYS", .fn = keys, .arity = 2},
	CommandInfo{.name = "TYPE", .fn = type, .arity = 2},
	CommandInfo{
		.name = "EXPIRE", .fn = expire_generic<after_seconds>, .arity = 3, .write = true},
	CommandInfo{
		.name = "PEXPIRE", .fn = expire_generic<after_millis>, .arity = 3, .write = true},
	CommandInfo{
		.name = "EXPIREAT", .fn = expire_generic<at_seconds>, .arity = 3, .write = true},
	CommandInfo{
		.name = "PEXPIREAT", .fn = expire_generic<at_millis>, .arity = 3, .write = true},
	CommandInfo{.name = "PERSIST", .fn = persist, .arity = 2, .write = true},
	CommandInfo{.name = "TTL", .fn = ttl_generic<false>, .arity = 2},
	CommandInfo{.name = "PTTL", .fn = ttl_generic<true>, .arity = 2},
	CommandInfo{.name = "FLUSHDB", .fn = flushdb, .arity = -1, .write = true},
	CommandInfo{.name = "DBSIZE", .fn = dbsize, .arity = 1},
};
} // namespace

std::span<const CommandInfo> keyspace_commands() { return COMMANDS; }

} // namespace redis
