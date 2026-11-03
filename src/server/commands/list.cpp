// List commands: LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE, LINDEX.
#include "command.hpp"
#include "server/commands/reply.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep
#include "server/storage/object.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace redis {

namespace {
enum class End : bool { Head, Tail };

template <End Where>
void push(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto list = ctx.db.find_or_create<List>(args[1]);
	if (!list) {
		out.wrong_type();
		return;
	}
	for (const std::string_view element : args.subspan(2))
		if constexpr (Where == End::Head) (*list)->emplace_front(element);
		else (*list)->emplace_back(element);
	out.integer(static_cast<int64_t>((*list)->size()));
}

// LPOP key [count] / RPOP key [count]. Without a count the reply is one
// element; with one it is an array of up to count elements.
template <End Where>
void pop(CommandContext &ctx, Args args, ReplyWriter &out) {
	if (args.size() > 3) {
		out.error(Where == End::Head
					  ? "ERR wrong number of arguments for 'lpop' command"
					  : "ERR wrong number of arguments for 'rpop' command");
		return;
	}

	std::optional<int64_t> count;
	if (args.size() == 3) {
		count = parse_int(args[2]);
		if (!count || *count < 0) {
			out.error("ERR value is out of range, must be positive");
			return;
		}
	}

	const std::string_view key = args[1];
	const auto list            = ctx.db.find_as<List>(key);
	if (!list) {
		out.wrong_type();
		return;
	}
	if (*list == nullptr) {
		out.nil();
		return;
	}

	// Replies with the element at the end, then drops it.
	auto take_one = [&] {
		auto &l = **list;
		if constexpr (Where == End::Head) {
			out.bulk(l.front());
			l.pop_front();
		} else {
			out.bulk(l.back());
			l.pop_back();
		}
	};

	if (!count) {
		take_one();
	} else {
		const auto n =
			std::min<size_t>(static_cast<size_t>(*count), (*list)->size());
		out.array(n);
		for (size_t i = 0; i < n; ++i) take_one();
	}
	ctx.db.erase_if_empty(key);
}

void llen(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) {
		out.wrong_type();
		return;
	}
	out.integer(((*list) != nullptr) ? static_cast<int64_t>((*list)->size())
									 : 0);
}

// Converts a Redis list index (negative counts from the tail) to an offset.
int64_t normalize_index(int64_t index, int64_t size) {
	return index < 0 ? index + size : index;
}

void lrange(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto start_arg = parse_int(args[2]);
	const auto stop_arg  = parse_int(args[3]);
	if (!start_arg || !stop_arg) {
		out.not_integer();
		return;
	}

	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) {
		out.wrong_type();
		return;
	}
	if (*list == nullptr) {
		out.array(0);
		return;
	}

	const auto size  = static_cast<int64_t>((*list)->size());
	const auto start = std::max<int64_t>(normalize_index(*start_arg, size), 0);
	const auto stop  = std::min(normalize_index(*stop_arg, size), size - 1);
	if (start > stop) {
		out.array(0);
		return;
	}

	out.array(static_cast<size_t>(stop - start + 1));
	for (auto i = start; i <= stop; ++i)
		out.bulk((**list)[static_cast<size_t>(i)]);
}

void lindex(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto index_arg = parse_int(args[2]);
	if (!index_arg) {
		out.not_integer();
		return;
	}

	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) {
		out.wrong_type();
		return;
	}
	if (*list == nullptr) {
		out.nil();
		return;
	}

	const auto size  = static_cast<int64_t>((*list)->size());
	const auto index = normalize_index(*index_arg, size);
	if (index < 0 || index >= size) {
		out.nil();
		return;
	}
	out.bulk((**list)[static_cast<size_t>(index)]);
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "LPUSH", .fn = push<End::Head>, .arity = -3},
	CommandInfo{.name = "RPUSH", .fn = push<End::Tail>, .arity = -3},
	CommandInfo{.name = "LPOP", .fn = pop<End::Head>, .arity = -2},
	CommandInfo{.name = "RPOP", .fn = pop<End::Tail>, .arity = -2},
	CommandInfo{.name = "LLEN", .fn = llen, .arity = 2},
	CommandInfo{.name = "LRANGE", .fn = lrange, .arity = 4},
	CommandInfo{.name = "LINDEX", .fn = lindex, .arity = 3},
};
} // namespace

std::span<const CommandInfo> list_commands() { return COMMANDS; }

} // namespace redis
