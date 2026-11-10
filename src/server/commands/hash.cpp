// Hash commands: HSET, HGET, HDEL, HGETALL, HLEN, HEXISTS, HMGET.
#include "command.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep
#include "common/store/object.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <version> // IWYU pragma: keep (feature-test macros)

namespace redis {
using resp::Value;

namespace {
Value hset(CommandContext &ctx, Args args) {
	if (args.size() % 2 != 0) {
		return reply::error("ERR wrong number of arguments for 'hset' command");
	}
	const auto hash = ctx.db.find_or_create<Hash>(args[1]);
	if (!hash) {
		return reply::wrong_type();
	}

	int64_t added = 0;
	for (size_t i = 2; i < args.size(); i += 2) {
		const auto [_, inserted] =
			(*hash)->insert_or_assign(String(args[i]), String(args[i + 1]));
		added += inserted ? 1 : 0;
	}
	return reply::integer(added);
}

Value hget(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		return reply::wrong_type();
	}
	if (*hash == nullptr) {
		return reply::nil();
	}
	const auto it = (*hash)->find(args[2]);
	return it == (*hash)->end() ? reply::nil() : reply::bulk(it->second);
}

Value hmget(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		return reply::wrong_type();
	}
	Value::Array values;
	values.reserve(args.size() - 2);
	for (const std::string_view field : args.subspan(2)) {
		if (*hash == nullptr) {
			values.push_back(reply::nil());
			continue;
		}
		const auto it = (*hash)->find(field);
		values.push_back(it == (*hash)->end() ? reply::nil()
											  : reply::bulk(it->second));
	}
	return reply::array(std::move(values));
}

Value hdel(CommandContext &ctx, Args args) {
	const std::string_view key = args[1];
	const auto hash            = ctx.db.find_as<Hash>(key);
	if (!hash) {
		return reply::wrong_type();
	}
	if (*hash == nullptr) {
		return reply::integer(0);
	}

	int64_t removed = 0;
	for (const std::string_view field : args.subspan(2))
#if defined(__cpp_lib_associative_heterogeneous_erasure) &&                    \
	__cpp_lib_associative_heterogeneous_erasure >= 202110L
		removed += static_cast<int64_t>((*hash)->erase(field));
#else
		// Fallback find() + erase(it) idiom (works on LLVM libc++)
		if (auto it = (*hash)->find(field); it != (*hash)->end()) {
			(*hash)->erase(it);
			removed += 1;
		}
#endif
	ctx.db.erase_if_empty(key);
	return reply::integer(removed);
}

Value hgetall(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		return reply::wrong_type();
	}
	if (*hash == nullptr) {
		return reply::array({});
	}
	Value::Array pairs;
	pairs.reserve((*hash)->size() * 2);
	for (const auto &[field, value] : **hash) {
		pairs.push_back(reply::bulk(field));
		pairs.push_back(reply::bulk(value));
	}
	return reply::array(std::move(pairs));
}

Value hlen(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		return reply::wrong_type();
	}
	return reply::integer(((*hash) != nullptr) ? static_cast<int64_t>((*hash)->size())
									 : 0);
}

Value hexists(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		return reply::wrong_type();
	}
	return reply::integer(((*hash) != nullptr) && (*hash)->contains(args[2]) ? 1 : 0);
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "HSET", .fn = hset, .arity = -4, .write = true},
	CommandInfo{.name = "HGET", .fn = hget, .arity = 3},
	CommandInfo{.name = "HMGET", .fn = hmget, .arity = -3},
	CommandInfo{.name = "HDEL", .fn = hdel, .arity = -3, .write = true},
	CommandInfo{.name = "HGETALL", .fn = hgetall, .arity = 2},
	CommandInfo{.name = "HLEN", .fn = hlen, .arity = 2},
	CommandInfo{.name = "HEXISTS", .fn = hexists, .arity = 3},
};
} // namespace

std::span<const CommandInfo> hash_commands() { return COMMANDS; }

} // namespace redis
