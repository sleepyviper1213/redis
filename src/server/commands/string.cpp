// String commands: SET, GET, INCR and friends, MSET/MGET, APPEND, STRLEN.
#include "command.hpp"
#include "core/deadline_timer.hpp"
#include "core/utils/saturating.hpp"
#include "core/utils/toupper.hpp"
#include "server/commands/reply.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/database.hpp" // IWYU pragma: keep
#include "server/database_fwd.hpp"
#include "core/object.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace redis {

namespace {
using std::chrono::milliseconds;
using std::chrono::seconds;

struct SetOptions {
	bool nx = false, xx = false, get = false, keep_ttl = false;
	std::optional<DeadlineTimer::time_point> deadline;
};

// Parses SET's options. On failure returns the error message to reply with.
std::expected<SetOptions, std::string_view> parse_set_options(Args args) {
	SetOptions opts;
	bool has_expiry = false; // EX, PX, EXAT, PXAT or KEEPTTL seen

	for (size_t i = 3; i < args.size(); ++i) {
		const std::string opt = toupper(std::string(args[i]));
		if (opt == "NX" && !opts.xx) {
			opts.nx = true;
		} else if (opt == "XX" && !opts.nx) {
			opts.xx = true;
		} else if (opt == "GET") {
			opts.get = true;
		} else if (opt == "KEEPTTL" && !has_expiry) {
			has_expiry = opts.keep_ttl = true;
		} else if ((opt == "EX" || opt == "PX" || opt == "EXAT" ||
					opt == "PXAT") &&
				   !has_expiry && i + 1 < args.size()) {
			has_expiry      = true;
			const auto time = parse_int(args[++i]);
			if (!time) return std::unexpected(ReplyWriter::NOT_INTEGER);
			if (*time <= 0)
				return std::unexpected(
					"ERR invalid expire time in 'set' command");

			const auto now = DeadlineTimer::now();
			if (opt == "EX") opts.deadline = now + seconds(*time);
			else if (opt == "PX") opts.deadline = now + milliseconds(*time);
			else if (opt == "EXAT")
				opts.deadline = DeadlineTimer::time_point(seconds(*time));
			else opts.deadline = DeadlineTimer::time_point(milliseconds(*time));
		} else {
			return std::unexpected(ReplyWriter::SYNTAX_ERROR);
		}
	}
	return opts;
}

void set(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto opts = parse_set_options(args);
	if (!opts) {
		out.error(opts.error());
		return;
	}

	const std::string_view key = args[1];

	// A plain SET (no NX, XX or GET) overwrites whatever is there, so it does
	// not need to look the key up first; the set() below does the one lookup.
	if (opts->nx || opts->xx || opts->get) {
		const auto current = ctx.db.find_string(key);
		// SET ... GET must read a string first.
		if (!current && opts->get) {
			out.wrong_type();
			return;
		}
		const bool exists = !current || *current != nullptr;

		// With GET, the reply is the previous value (or nil) instead of OK. It
		// is written now, before the write below replaces what it views.
		if (opts->get) {
			if (exists) out.bulk((*current)->string());
			else out.nil();
		}

		if ((opts->nx && exists) || (opts->xx && !exists)) {
			if (!opts->get) out.nil();
			return;
		}
	}

	ctx.db.set(key, String(args[2]));
	// Like Redis, a plain SET discards any previous TTL unless KEEPTTL.
	if (opts->deadline) ctx.db.set_expiry(key, DeadlineTimer::at(*opts->deadline));
	else if (!opts->keep_ttl) ctx.db.persist(key);

	// EX and PX are relative to now, which a replay does not share: log the
	// deadline itself. NX, XX and GET are dropped; they only decided to get
	// here. A deadline already past deleted the key again.
	if (ctx.aof != nullptr && opts->deadline) {
		if (DeadlineTimer::at(*opts->deadline).has_expired()) {
			ctx.aof->replace({"DEL", key});
		} else {
			const auto at = DeadlineTimer::at(*opts->deadline).millis_since_epoch();
			ctx.aof->replace({"SET", key, args[2], "PXAT", std::to_string(at)});
		}
	}

	if (!opts->get) out.ok();
}

void get(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto value = ctx.db.find_string(args[1]);
	if (!value) {
		out.wrong_type();
		return;
	}
	if (*value == nullptr) {
		out.nil();
		return;
	}
	out.bulk((*value)->string());
}

// INCR, DECR, INCRBY, DECRBY: the stored string must be an int64, and the
// result must not overflow.
void increment_by(CommandContext &ctx, std::string_view key, int64_t delta,
				  ReplyWriter &out) {
	const auto value = ctx.db.find_string(key);
	if (!value) {
		out.wrong_type();
		return;
	}

	int64_t current = 0;
	if (*value != nullptr) {
		const auto parsed = parse_int((*value)->string());
		if (!parsed) {
			out.not_integer();
			return;
		}
		current = *parsed;
	}

	int64_t result = 0;
	if (add_overflow(current, delta, &result)) {
		out.error("ERR increment or decrement would overflow");
		return;
	}

	if (*value != nullptr) (*value)->assign_string(std::to_string(result));
	else ctx.db.set(key, std::to_string(result));
	out.integer(result);
}

void incr(CommandContext &ctx, Args args, ReplyWriter &out) {
	increment_by(ctx, args[1], 1, out);
}

void decr(CommandContext &ctx, Args args, ReplyWriter &out) {
	increment_by(ctx, args[1], -1, out);
}

void incrby(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto delta = parse_int(args[2]);
	if (!delta) {
		out.not_integer();
		return;
	}
	increment_by(ctx, args[1], *delta, out);
}

void decrby(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto delta = parse_int(args[2]);
	// Negating INT64_MIN overflows, so it is out of range here.
	if (!delta || *delta == std::numeric_limits<int64_t>::min()) {
		out.not_integer();
		return;
	}
	increment_by(ctx, args[1], -*delta, out);
}

void mset(CommandContext &ctx, Args args, ReplyWriter &out) {
	if (args.size() % 2 != 1) {
		out.error("ERR wrong number of arguments for 'mset' command");
		return;
	}
	for (size_t i = 1; i < args.size(); i += 2) {
		ctx.db.set(args[i], String(args[i + 1]));
		ctx.db.persist(args[i]);
	}
	out.ok();
}

void mget(CommandContext &ctx, Args args, ReplyWriter &out) {
	out.array(args.size() - 1);
	for (const std::string_view key : args.subspan(1)) {
		// MGET reports keys of other types as nil instead of failing.
		const auto value = ctx.db.find_string(key);
		if (value && *value != nullptr) out.bulk((*value)->string());
		else out.nil();
	}
}

void append(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto value = ctx.db.find_or_create_string(args[1]);
	if (!value) {
		out.wrong_type();
		return;
	}
	String &string = (*value)->mutable_string();
	string.append(args[2]);
	out.integer(static_cast<int64_t>(string.size()));
}

void strlen(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto value = ctx.db.find_string(args[1]);
	if (!value) {
		out.wrong_type();
		return;
	}
	out.integer(((*value) != nullptr)
					? static_cast<int64_t>((*value)->string().size())
					: 0);
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "SET", .fn = set, .arity = -3, .write = true},
	CommandInfo{.name = "GET", .fn = get, .arity = 2},
	CommandInfo{.name = "INCR", .fn = incr, .arity = 2, .write = true},
	CommandInfo{.name = "DECR", .fn = decr, .arity = 2, .write = true},
	CommandInfo{.name = "INCRBY", .fn = incrby, .arity = 3, .write = true},
	CommandInfo{.name = "DECRBY", .fn = decrby, .arity = 3, .write = true},
	CommandInfo{.name = "MSET", .fn = mset, .arity = -3, .write = true},
	CommandInfo{.name = "MGET", .fn = mget, .arity = -2},
	CommandInfo{.name = "APPEND", .fn = append, .arity = 3, .write = true},
	CommandInfo{.name = "STRLEN", .fn = strlen, .arity = 2},
};
} // namespace

std::span<const CommandInfo> string_commands() { return COMMANDS; }

} // namespace redis
