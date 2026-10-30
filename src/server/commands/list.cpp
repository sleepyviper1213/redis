// List commands: LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE, LINDEX.
#include "command.hpp"
#include "server/storage/database.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace redis {
using resp::Value;

namespace {
enum class End : bool { Head, Tail };

template <End Where>
Value push(CommandContext &ctx, Args args) {
	const auto list = ctx.db.find_or_create<List>(args[1]);
	if (!list) return reply::wrong_type();
	for (const std::string_view element : args.subspan(2))
		if constexpr (Where == End::Head)
			(*list)->emplace_front(element);
		else (*list)->emplace_back(element);
	return reply::integer(static_cast<int64_t>((*list)->size()));
}

// LPOP key [count] / RPOP key [count]. Without a count the reply is one
// element; with one it is an array of up to count elements.
template <End Where>
Value pop(CommandContext &ctx, Args args) {
	if (args.size() > 3)
		return reply::error(
			Where == End::Head
				? "ERR wrong number of arguments for 'lpop' command"
				: "ERR wrong number of arguments for 'rpop' command");

	std::optional<int64_t> count;
	if (args.size() == 3) {
		count = parse_int(args[2]);
		if (!count || *count < 0)
			return reply::error("ERR value is out of range, must be positive");
	}

	const std::string_view key = args[1];
	const auto list        = ctx.db.find_as<List>(key);
	if (!list) return reply::wrong_type();
	if (*list == nullptr) return reply::nil();

	auto take_one = [&] {
		auto &l = **list;
		String element;
		if constexpr (Where == End::Head) {
			element = std::move(l.front());
			l.pop_front();
		} else {
			element = std::move(l.back());
			l.pop_back();
		}
		return element;
	};

	Value result = reply::nil();
	if (!count) {
		result = reply::bulk(take_one());
	} else {
		const auto n =
			std::min<size_t>(static_cast<size_t>(*count), (*list)->size());
		Value::Array elements;
		elements.reserve(n);
		for (size_t i = 0; i < n; ++i)
			elements.push_back(reply::bulk(take_one()));
		result = Value::from_array(std::move(elements));
	}
	ctx.db.erase_if_empty(key);
	return result;
}

Value llen(CommandContext &ctx, Args args) {
	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) return reply::wrong_type();
	return reply::integer(*list ? static_cast<int64_t>((*list)->size()) : 0);
}

// Converts a Redis list index (negative counts from the tail) to an offset.
int64_t normalize_index(int64_t index, int64_t size) {
	return index < 0 ? index + size : index;
}

Value lrange(CommandContext &ctx, Args args) {
	const auto start_arg = parse_int(args[2]);
	const auto stop_arg  = parse_int(args[3]);
	if (!start_arg || !stop_arg) return reply::not_integer();

	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) return reply::wrong_type();
	if (*list == nullptr) return Value::from_array({});

	const auto size  = static_cast<int64_t>((*list)->size());
	const auto start = std::max<int64_t>(normalize_index(*start_arg, size), 0);
	const auto stop  = std::min(normalize_index(*stop_arg, size), size - 1);
	if (start > stop) return Value::from_array({});

	Value::Array elements;
	elements.reserve(static_cast<size_t>(stop - start + 1));
	for (auto i = start; i <= stop; ++i)
		elements.push_back(reply::bulk((**list)[static_cast<size_t>(i)]));
	return Value::from_array(std::move(elements));
}

Value lindex(CommandContext &ctx, Args args) {
	const auto index_arg = parse_int(args[2]);
	if (!index_arg) return reply::not_integer();

	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) return reply::wrong_type();
	if (*list == nullptr) return reply::nil();

	const auto size  = static_cast<int64_t>((*list)->size());
	const auto index = normalize_index(*index_arg, size);
	if (index < 0 || index >= size) return reply::nil();
	return reply::bulk((**list)[static_cast<size_t>(index)]);
}

constexpr std::array COMMANDS{
	CommandInfo{"LPUSH", push<End::Head>, -3},
	CommandInfo{"RPUSH", push<End::Tail>, -3},
	CommandInfo{"LPOP", pop<End::Head>, -2},
	CommandInfo{"RPOP", pop<End::Tail>, -2},
	CommandInfo{"LLEN", llen, 2},
	CommandInfo{"LRANGE", lrange, 4},
	CommandInfo{"LINDEX", lindex, 3},
};
} // namespace

std::span<const CommandInfo> list_commands() { return COMMANDS; }

} // namespace redis
