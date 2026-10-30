// Hash commands: HSET, HGET, HDEL, HGETALL, HLEN, HEXISTS, HMGET.
#include "command.hpp"
#include "server/storage/database.hpp"

#include <array>
#include <string>

namespace redis {
using resp::Value;

namespace {
Value hset(CommandContext &ctx, Args args) {
	if (args.size() % 2 != 0)
		return reply::error("ERR wrong number of arguments for 'hset' command");
	const auto hash = ctx.db.find_or_create<Hash>(args[1]);
	if (!hash) return reply::wrong_type();

	int64_t added = 0;
	for (size_t i = 2; i < args.size(); i += 2) {
		const auto [_, inserted] =
			(*hash)->insert_or_assign(std::string(args[i]),
									  std::string(args[i + 1]));
		added += inserted ? 1 : 0;
	}
	return reply::integer(added);
}

Value hget(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return reply::wrong_type();
	if (*hash == nullptr) return reply::nil();
	const auto it = (*hash)->find(args[2]);
	return it == (*hash)->end() ? reply::nil() : reply::bulk(it->second);
}

Value hmget(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return reply::wrong_type();
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
	return Value::from_array(std::move(values));
}

Value hdel(CommandContext &ctx, Args args) {
	const std::string_view key = args[1];
	const auto hash        = ctx.db.find_as<Hash>(key);
	if (!hash) return reply::wrong_type();
	if (*hash == nullptr) return reply::integer(0);

	int64_t removed = 0;
	for (const std::string_view field : args.subspan(2))
		removed += static_cast<int64_t>((*hash)->erase(field));
	ctx.db.erase_if_empty(key);
	return reply::integer(removed);
}

Value hgetall(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return reply::wrong_type();
	Value::Array flat;
	if (*hash != nullptr) {
		flat.reserve((*hash)->size() * 2);
		for (const auto &[field, value] : **hash) {
			flat.push_back(reply::bulk(field));
			flat.push_back(reply::bulk(value));
		}
	}
	return Value::from_array(std::move(flat));
}

Value hlen(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return reply::wrong_type();
	return reply::integer(*hash ? static_cast<int64_t>((*hash)->size()) : 0);
}

Value hexists(CommandContext &ctx, Args args) {
	const auto hash = ctx.db.find_as<Hash>(args[1]);
	if (!hash) return reply::wrong_type();
	return reply::integer(*hash && (*hash)->contains(args[2]) ? 1
																		  : 0);
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
