// Hash commands: HSET, HGET, HDEL, HGETALL, HLEN, HEXISTS, HMGET.
#include "command.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep

#include <array>
#include <string>
#include <version>

namespace redis {

namespace {
void hset(CommandContext &ctx, Args args, ReplyWriter &out) {
	if (args.size() % 2 != 0)
		return out.error("ERR wrong number of arguments for 'hset' command");
	const auto hash = ctx.db.find_or_create<Hash>(args[1]);
	if (!hash) return out.wrong_type();

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
	if (!hash) return out.wrong_type();
	if (*hash == nullptr) return out.nil();
	const auto it = (*hash)->find(args[2]);
	if (it == (*hash)->end()) out.nil();
	else out.bulk(it->second);
}

void hmget(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return out.wrong_type();
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
	if (!hash) return out.wrong_type();
	if (*hash == nullptr) return out.integer(0);

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
	if (!hash) return out.wrong_type();
	if (*hash == nullptr) return out.array(0);
	out.array((*hash)->size() * 2);
	for (const auto &[field, value] : **hash) {
		out.bulk(field);
		out.bulk(value);
	}
}

void hlen(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return out.wrong_type();
	out.integer(*hash ? static_cast<int64_t>((*hash)->size()) : 0);
}

void hexists(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return out.wrong_type();
	out.integer(*hash && (*hash)->contains(args[2]) ? 1 : 0);
}

constexpr std::array COMMANDS{
	CommandInfo{"HSET", hset, -4},
	CommandInfo{"HGET", hget, 3},
	CommandInfo{"HMGET", hmget, -3},
	CommandInfo{"HDEL", hdel, -3},
	CommandInfo{"HGETALL", hgetall, 2},
	CommandInfo{"HLEN", hlen, 2},
	CommandInfo{"HEXISTS", hexists, 3},
};
} // namespace

std::span<const CommandInfo> hash_commands() { return COMMANDS; }

} // namespace redis
