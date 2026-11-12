// String commands: SET, GET, INCR and friends, MSET/MGET, APPEND, STRLEN.
#include "command.hpp"
#include "core/error/failed.hpp"
#include "core/utils/parse_number.hpp"
#include "core/chrono/date_time.hpp"
#include "core/chrono/deadline_timer.hpp"
#include "common/store/object.hpp"
#include "core/utils/ascii.hpp"
#include "core/utils/saturating.hpp"
#include "detail/expire_time.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep
#include "common/store/database_fwd.hpp"
#include "server/persistence/append_only_file.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace redis {
using resp::Value;

namespace {
using std::chrono::milliseconds;
using std::chrono::seconds;

struct SetOptions {
	bool nx = false, xx = false, get = false, keep_ttl = false;
	std::optional<DateTime> deadline;
};

/// A SET option that takes an expiry as its next argument, and what that
/// argument means.
struct ExpiryOption {
	std::string_view name;
	detail::ExpireUnit unit;
	detail::ExpireBase base;
};

constexpr std::array EXPIRY_OPTIONS{
	ExpiryOption{.name = "EX",
				 .unit = detail::ExpireUnit::Seconds,
				 .base = detail::ExpireBase::Relative},
	ExpiryOption{.name = "PX",
				 .unit = detail::ExpireUnit::Milliseconds,
				 .base = detail::ExpireBase::Relative},
	ExpiryOption{.name = "EXAT",
				 .unit = detail::ExpireUnit::Seconds,
				 .base = detail::ExpireBase::Absolute},
	ExpiryOption{.name = "PXAT",
				 .unit = detail::ExpireUnit::Milliseconds,
				 .base = detail::ExpireBase::Absolute},
};

/// Why SET's options were refused. What the client is told is chosen where the
/// reply is written, not here.
enum class SetError : std::uint8_t {
	NotInteger,    ///< an expiry that is not a whole number
	Syntax,        ///< an option that does not exist, or one too many
	InvalidExpire, ///< an expiry that is not positive, or overflows
};

// Parses SET's options. On failure returns why.
std::expected<SetOptions, SetError> parse_set_options(Args args) {
	SetOptions opts;
	bool has_expiry = false; // EX, PX, EXAT, PXAT or KEEPTTL seen

	for (size_t i = 3; i < args.size(); ++i) {
		// Looked up before the loop variable moves on to the option's value.
		const std::string_view arg = args[i];
		// An iterator, not a pointer: only libstdc++'s std::array iterator is
		// a raw pointer, and MSVC's STL rejects `const auto *` here.
		const auto expiry =
			std::ranges::find_if(EXPIRY_OPTIONS,
								 [arg](const ExpiryOption &option) {
									 return iequals(arg, option.name);
								 });
		if (iequals(arg, "NX") && !opts.xx) {
			opts.nx = true;
		} else if (iequals(arg, "XX") && !opts.nx) {
			opts.xx = true;
		} else if (iequals(arg, "GET")) {
			opts.get = true;
		} else if (iequals(arg, "KEEPTTL") && !has_expiry) {
			has_expiry = opts.keep_ttl = true;
		} else if (expiry != EXPIRY_OPTIONS.end() && !has_expiry &&
				   i + 1 < args.size()) {
			has_expiry      = true;
			const auto time = parse_integer(args[++i]);
			if (!time) REDIS_FAILED(SetError::NotInteger);
			const auto timer =
				*time > 0 ? detail::expire_time(*time, expiry->unit, expiry->base)
						  : std::nullopt;
			if (!timer) REDIS_FAILED(SetError::InvalidExpire);
			opts.deadline = timer->deadline();
		} else {
			REDIS_FAILED(SetError::Syntax);
		}
	}
	return opts;
}

Value set(CommandContext &ctx, Args args) {
	const auto opts = parse_set_options(args);
	if (!opts) {
		switch (opts.error()) {
		case SetError::NotInteger: return reply::not_integer();
		case SetError::Syntax: return reply::syntax_error();
		case SetError::InvalidExpire:
			return reply::error("ERR invalid expire time in 'set' command");
		}
		std::unreachable();
	}

	const std::string_view key = args[1];

	// A plain SET (no NX, XX or GET) overwrites whatever is there, so it does
	// not need to look the key up first; the set() below does the one lookup.
	Value previous = reply::nil(); // the reply of SET ... GET
	if (opts->nx || opts->xx || opts->get) {
		const auto current = ctx.db.find_string(key);
		// SET ... GET must read a string first.
		if (!current && opts->get) {
			return reply::wrong_type();
		}
		const bool exists = !current || *current != nullptr;

		// With GET, the reply is the previous value (or nil) instead of OK. It
		// is copied now, before the write below replaces what it views.
		if (opts->get)
			previous = exists ? reply::bulk((*current)->string()) : reply::nil();

		if ((opts->nx && exists) || (opts->xx && !exists))
			return opts->get ? std::move(previous) : reply::nil();
	}

	ctx.db.set(key, String(args[2]));
	// Like Redis, a plain SET discards any previous TTL unless KEEPTTL.
	if (opts->deadline)
		ctx.db.set_expiry(key, DeadlineTimer::at(*opts->deadline));
	else if (!opts->keep_ttl) ctx.db.persist(key);

	// EX and PX are relative to now, which a replay does not share: log the
	// deadline itself. NX, XX and GET are dropped; they only decided to get
	// here. A deadline already past deleted the key again.
	if (ctx.aof != nullptr && opts->deadline) {
		if (DeadlineTimer::at(*opts->deadline).has_expired()) {
			ctx.aof->replace({"DEL", key});
		} else {
			const auto at =
				DeadlineTimer::at(*opts->deadline).millis_since_epoch();
			ctx.aof->replace({"SET", key, args[2], "PXAT", fmt::to_string(at)});
		}
	}

	return opts->get ? std::move(previous) : reply::ok();
}

Value get(CommandContext &ctx, Args args) {
	const auto value = ctx.db.find_string(args[1]);
	if (!value) {
		return reply::wrong_type();
	}
	if (*value == nullptr) {
		return reply::nil();
	}
	return reply::bulk((*value)->string());
}

// INCR, DECR, INCRBY, DECRBY: the stored string must be an int64, and the
// result must not overflow.
Value increment_by(CommandContext &ctx, std::string_view key, int64_t delta) {
	const auto value = ctx.db.find_string(key);
	if (!value) {
		return reply::wrong_type();
	}

	int64_t current = 0;
	if (*value != nullptr) {
		const auto parsed = parse_integer((*value)->string());
		if (!parsed) {
			return reply::not_integer();
		}
		current = *parsed;
	}

	int64_t result = 0;
	if (add_overflow(current, delta, &result)) {
		return reply::error("ERR increment or decrement would overflow");
	}

	if (*value != nullptr) { (*value)->assign_string(fmt::to_string(result));
	} else {
		const fmt::format_int digits(result);
		ctx.db.set(key, String(digits.data(), digits.size()));
	}
	return reply::integer(result);
}

Value incr(CommandContext &ctx, Args args) {
	return increment_by(ctx, args[1], 1);
}

Value decr(CommandContext &ctx, Args args) {
	return increment_by(ctx, args[1], -1);
}

Value incrby(CommandContext &ctx, Args args) {
	const auto delta = parse_integer(args[2]);
	if (!delta) {
		return reply::not_integer();
	}
	return increment_by(ctx, args[1], *delta);
}

Value decrby(CommandContext &ctx, Args args) {
	const auto delta = parse_integer(args[2]);
	// Negating INT64_MIN overflows, so it is out of range here.
	if (!delta || *delta == std::numeric_limits<int64_t>::min()) {
		return reply::not_integer();
	}
	return increment_by(ctx, args[1], -*delta);
}

Value mset(CommandContext &ctx, Args args) {
	if (args.size() % 2 != 1) {
		return reply::error("ERR wrong number of arguments for 'mset' command");
	}
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
							 ? reply::bulk((*value)->string())
							 : reply::nil());
	}
	return reply::array(std::move(values));
}

Value append(CommandContext &ctx, Args args) {
	const auto value = ctx.db.find_or_create_string(args[1]);
	if (!value) {
		return reply::wrong_type();
	}
	String &string = (*value)->mutable_string();
	string.append(args[2]);
	return reply::integer(static_cast<int64_t>(string.size()));
}

Value strlen(CommandContext &ctx, Args args) {
	const auto value = ctx.db.find_string(args[1]);
	if (!value) {
		return reply::wrong_type();
	}
	return reply::integer(((*value) != nullptr)
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
