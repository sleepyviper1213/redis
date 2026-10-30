// String commands: SET, GET, INCR and friends, MSET/MGET, APPEND, STRLEN.
#include "command.hpp"
#include "core/utils/saturating.hpp"
#include "core/utils/toupper.hpp"
#include "server/storage/database.hpp"

#include <array>
#include <chrono>
#include <limits>
#include <optional>
#include <string>

namespace redis {
using resp::Value;

namespace {
using std::chrono::milliseconds;
using std::chrono::seconds;

struct SetOptions {
	bool nx = false, xx = false, get = false, keep_ttl = false;
	std::optional<Database::time_point> deadline;
};

// Parses SET's options. On failure returns the error reply to send.
std::expected<SetOptions, Value> parse_set_options(Args args) {
	SetOptions opts;
	bool has_expiry = false; // EX, PX, EXAT, PXAT or KEEPTTL seen

	for (size_t i = 3; i < args.size(); ++i) {
		const std::string opt = toupper(std::string(args[i]));
		if (opt == "NX" && !opts.xx) opts.nx = true;
		else if (opt == "XX" && !opts.nx) opts.xx = true;
		else if (opt == "GET") opts.get = true;
		else if (opt == "KEEPTTL" && !has_expiry)
			has_expiry = opts.keep_ttl = true;
		else if ((opt == "EX" || opt == "PX" || opt == "EXAT" ||
				  opt == "PXAT") &&
				 !has_expiry && i + 1 < args.size()) {
			has_expiry      = true;
			const auto time = parse_int(args[++i]);
			if (!time) return std::unexpected(reply::not_integer());
			if (*time <= 0)
				return std::unexpected(
					reply::error("ERR invalid expire time in 'set' command"));

			const auto now = Database::clock::now();
			if (opt == "EX") opts.deadline = now + seconds(*time);
			else if (opt == "PX") opts.deadline = now + milliseconds(*time);
			else if (opt == "EXAT")
				opts.deadline = Database::time_point(seconds(*time));
			else opts.deadline = Database::time_point(milliseconds(*time));
		} else {
			return std::unexpected(reply::syntax_error());
		}
	}
	return opts;
}

Value set(CommandContext &ctx, Args args) {
	const auto opts = parse_set_options(args);
	if (!opts) return opts.error();

	const std::string_view key = args[1];

	// With GET, the reply is the previous value (or nil) instead of OK.
	Value previous = reply::nil();

	// A plain SET (no NX, XX or GET) overwrites whatever is there, so it does
	// not need to look the key up first; the set() below does the one lookup.
	if (opts->nx || opts->xx || opts->get) {
		const auto current = ctx.db.find_string(key);
		// SET ... GET must read a string first.
		if (!current && opts->get) return reply::wrong_type();
		const bool exists = !current || *current != nullptr;

		if (opts->get && exists)
			previous = reply::bulk(std::string((*current)->string()));

		if ((opts->nx && exists) || (opts->xx && !exists))
			return opts->get ? previous : reply::nil();
	}

	ctx.db.set(key, String(args[2]));
	// Like Redis, a plain SET discards any previous TTL unless KEEPTTL.
	if (opts->deadline) ctx.db.set_expiry(key, *opts->deadline);
	else if (!opts->keep_ttl) ctx.db.persist(key);

	return opts->get ? previous : reply::ok();
}

Value get(CommandContext &ctx, Args args) {
	const auto value = ctx.db.find_string(args[1]);
	if (!value) return reply::wrong_type();
	if (*value == nullptr) return reply::nil();
	return reply::bulk(std::string((*value)->string()));
}

// INCR, DECR, INCRBY, DECRBY: the stored string must be an int64, and the
// result must not overflow.
Value increment_by(CommandContext &ctx, std::string_view key, int64_t delta) {
	const auto value = ctx.db.find_string(key);
	if (!value) return reply::wrong_type();

	int64_t current = 0;
	if (*value != nullptr) {
		const auto parsed = parse_int((*value)->string());
		if (!parsed) return reply::not_integer();
		current = *parsed;
	}

	int64_t result = 0;
	if (add_overflow(current, delta, &result))
		return reply::error("ERR increment or decrement would overflow");

	if (*value != nullptr) (*value)->assign_string(std::to_string(result));
	else ctx.db.set(key, std::to_string(result));
	return reply::integer(result);
}

Value incr(CommandContext &ctx, Args args) {
	return increment_by(ctx, args[1], 1);
}

Value decr(CommandContext &ctx, Args args) {
	return increment_by(ctx, args[1], -1);
}

Value incrby(CommandContext &ctx, Args args) {
	const auto delta = parse_int(args[2]);
	if (!delta) return reply::not_integer();
	return increment_by(ctx, args[1], *delta);
}

Value decrby(CommandContext &ctx, Args args) {
	const auto delta = parse_int(args[2]);
	// Negating INT64_MIN overflows, so it is out of range here.
	if (!delta || *delta == std::numeric_limits<int64_t>::min())
		return reply::not_integer();
	return increment_by(ctx, args[1], -*delta);
}

Value mset(CommandContext &ctx, Args args) {
	if (args.size() % 2 != 1)
		return reply::error("ERR wrong number of arguments for 'mset' command");
	for (size_t i = 1; i < args.size(); i += 2) {
		ctx.db.set(args[i], String(args[i + 1]));
		ctx.db.persist(args[i]);
	}
	return reply::ok();
}

Value mget(CommandContext &ctx, Args args) {
	Value::Array values;
	values.reserve(args.size() - 1);
	for (const std::string_view key : args.subspan(1)) {
		// MGET reports keys of other types as nil instead of failing.
		const auto value = ctx.db.find_string(key);
		values.push_back(value && *value != nullptr
							 ? reply::bulk(std::string((*value)->string()))
							 : reply::nil());
	}
	return Value::from_array(std::move(values));
}

Value append(CommandContext &ctx, Args args) {
	const auto value = ctx.db.find_or_create_string(args[1]);
	if (!value) return reply::wrong_type();
	String &string = (*value)->mutable_string();
	string.append(args[2]);
	return reply::integer(static_cast<int64_t>(string.size()));
}

Value strlen(CommandContext &ctx, Args args) {
	const auto value = ctx.db.find_string(args[1]);
	if (!value) return reply::wrong_type();
	return reply::integer(
		*value ? static_cast<int64_t>((*value)->string().size()) : 0);
}

constexpr std::array COMMANDS{
	CommandInfo{"SET", set, -3},
	CommandInfo{"GET", get, 2},
	CommandInfo{"INCR", incr, 2},
	CommandInfo{"DECR", decr, 2},
	CommandInfo{"INCRBY", incrby, 3},
	CommandInfo{"DECRBY", decrby, 3},
	CommandInfo{"MSET", mset, -3},
	CommandInfo{"MGET", mget, -2},
	CommandInfo{"APPEND", append, 3},
	CommandInfo{"STRLEN", strlen, 2},
};
} // namespace

std::span<const CommandInfo> string_commands() { return COMMANDS; }

} // namespace redis
