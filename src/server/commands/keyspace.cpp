// Commands that act on keys regardless of their type: DEL, EXISTS, KEYS,
// TYPE, expiry (EXPIRE family, PERSIST, TTL, PTTL), FLUSHDB, DBSIZE.
#include "command.hpp"
#include "core/utils/glob.hpp"
#include "server/storage/database.hpp"

#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace redis {
using resp::Value;

namespace {
using std::chrono::milliseconds;
using std::chrono::seconds;

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
	Value::Array matches;
	ctx.db.for_each([&](std::string_view key, auto &&...) {
		if (glob_match(pattern, key))
			matches.push_back(reply::bulk(std::string(key)));
	});
	return Value::from_array(std::move(matches));
}

Value type(CommandContext &ctx, Args args) {
	const Object *object = ctx.db.find(args[1]);
	return Value::from_simple_string(
		std::string(object ? type_name(*object) : "none"));
}

// EXPIRE, PEXPIRE, EXPIREAT and PEXPIREAT differ only in how the argument
// becomes a deadline.
template <auto ToDeadline>
Value expire_generic(CommandContext &ctx, Args args) {
	const auto amount = parse_int(args[2]);
	if (!amount) return reply::not_integer();
	return reply::integer(
		ctx.db.set_expiry(args[1], ToDeadline(*amount)) ? 1 : 0);
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

Value persist(CommandContext &ctx, Args args) {
	const std::string_view key = args[1];
	return reply::integer(ctx.db.contains(key) && ctx.db.persist(key) ? 1 : 0);
}

// TTL and PTTL reply -2 for a missing key and -1 for a key without expiry,
// as integers, so clients can tell the cases apart.
template <bool InMillis>
Value ttl_generic(CommandContext &ctx, Args args) {
	const std::string_view key = args[1];
	if (!ctx.db.contains(key)) return reply::integer(-2);
	const auto deadline = ctx.db.expiry(key);
	if (!deadline) return reply::integer(-1);

	const auto left = std::chrono::duration_cast<milliseconds>(
						  *deadline - Database::clock::now())
						  .count();
	// Round to the nearest second like Redis; truncating would report 99
	// immediately after SET ... EX 100.
	return reply::integer(InMillis ? left : (left + 500) / 1000);
}

Value flushdb(CommandContext &ctx, Args) {
	ctx.db.clear();
	return reply::ok();
}

Value dbsize(CommandContext &ctx, Args) {
	return reply::integer(static_cast<int64_t>(ctx.db.size()));
}

constexpr std::array COMMANDS{
	CommandInfo{"DEL", del, -2},
	CommandInfo{"EXISTS", exists, -2},
	CommandInfo{"KEYS", keys, 2},
	CommandInfo{"TYPE", type, 2},
	CommandInfo{"EXPIRE", expire_generic<after_seconds>, 3},
	CommandInfo{"PEXPIRE", expire_generic<after_millis>, 3},
	CommandInfo{"EXPIREAT", expire_generic<at_seconds>, 3},
	CommandInfo{"PEXPIREAT", expire_generic<at_millis>, 3},
	CommandInfo{"PERSIST", persist, 2},
	CommandInfo{"TTL", ttl_generic<false>, 2},
	CommandInfo{"PTTL", ttl_generic<true>, 2},
	CommandInfo{"FLUSHDB", flushdb, -1},
	CommandInfo{"DBSIZE", dbsize, 1},
};
} // namespace

std::span<const CommandInfo> keyspace_commands() { return COMMANDS; }

} // namespace redis
