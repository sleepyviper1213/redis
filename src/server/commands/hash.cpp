// Hash commands: HSET, HGET, HDEL, HGETALL, HLEN, HEXISTS, HMGET.
#include "command.hpp"
#include "server/commands/reply.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep
#include "server/storage/object.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <version> // IWYU pragma: keep (feature-test macros)

namespace redis {

namespace {
void hset(CommandContext &ctx, Args args, ReplyWriter &out) {
	if (args.size() % 2 != 0) {
		out.error("ERR wrong number of arguments for 'hset' command");
		return;
	}
	const auto hash = ctx.db.find_or_create<Hash>(args[1]);
	if (!hash) {
		out.wrong_type();
		return;
	}

	int64_t added = 0;
	for (size_t i = 2; i < args.size(); i += 2) {
		const auto [_, inserted] =
			(*hash)->insert_or_assign(String(args[i]), String(args[i + 1]));
		added += inserted ? 1 : 0;
	}
	out.integer(added);
}

void hget(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		out.wrong_type();
		return;
	}
	if (*hash == nullptr) {
		out.nil();
		return;
	}
	const auto it = (*hash)->find(args[2]);
	if (it == (*hash)->end()) out.nil();
	else out.bulk(it->second);
}

void hmget(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		out.wrong_type();
		return;
	}
	out.array(args.size() - 2);
	for (const std::string_view field : args.subspan(2)) {
		if (*hash == nullptr) {
			out.nil();
			continue;
		}
		const auto it = (*hash)->find(field);
		if (it == (*hash)->end()) out.nil();
		else out.bulk(it->second);
	}
}

void hdel(CommandContext &ctx, Args args, ReplyWriter &out) {
	const std::string_view key = args[1];
	const auto hash            = ctx.db.find_as<Hash>(key);
	if (!hash) {
		out.wrong_type();
		return;
	}
	if (*hash == nullptr) {
		out.integer(0);
		return;
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
	out.integer(removed);
}

void hgetall(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		out.wrong_type();
		return;
	}
	if (*hash == nullptr) {
		out.array(0);
		return;
	}
	out.array((*hash)->size() * 2);
	for (const auto &[field, value] : **hash) {
		out.bulk(field);
		out.bulk(value);
	}
}

void hlen(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		out.wrong_type();
		return;
	}
	out.integer(((*hash) != nullptr) ? static_cast<int64_t>((*hash)->size())
									 : 0);
}

void hexists(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) {
		out.wrong_type();
		return;
	}
	out.integer(((*hash) != nullptr) && (*hash)->contains(args[2]) ? 1 : 0);
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "HSET", .fn = hset, .arity = -4},
	CommandInfo{.name = "HGET", .fn = hget, .arity = 3},
	CommandInfo{.name = "HMGET", .fn = hmget, .arity = -3},
	CommandInfo{.name = "HDEL", .fn = hdel, .arity = -3},
	CommandInfo{.name = "HGETALL", .fn = hgetall, .arity = 2},
	CommandInfo{.name = "HLEN", .fn = hlen, .arity = 2},
	CommandInfo{.name = "HEXISTS", .fn = hexists, .arity = 3},
};
} // namespace

std::span<const CommandInfo> hash_commands() { return COMMANDS; }

} // namespace redis
