// List commands: LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE, LINDEX.
#include "command.hpp"
#include "core/utils/parse_number.hpp"
#include "common/store/object.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace redis {
using resp::Value;

namespace {
enum class End : bool { Head, Tail };

template <End Where>
Value push(CommandContext &ctx, Args args) {
	const auto list = ctx.db.find_or_create<List>(args[1]);
	if (!list) {
		return reply::wrong_type();
	}
	for (const std::string_view element : args.subspan(2))
		if constexpr (Where == End::Head) (*list)->emplace_front(element);
		else (*list)->emplace_back(element);
	return reply::integer(static_cast<int64_t>((*list)->size()));
}

// LPOP key [count] / RPOP key [count]. Without a count the reply is one
// element; with one it is an array of up to count elements.
template <End Where>
Value pop(CommandContext &ctx, Args args) {
	if (args.size() > 3) {
		return reply::error("ERR wrong number of arguments for '{}' command",
							Where == End::Head ? "lpop" : "rpop");
	}

	std::optional<int64_t> count;
	if (args.size() == 3) {
		const auto parsed = parse_integer(args[2]);
		if (!parsed || *parsed < 0) {
			return reply::error("ERR value is out of range, must be positive");
		}
		count = *parsed;
	}

	const std::string_view key = args[1];
	const auto list            = ctx.db.find_as<List>(key);
	if (!list) {
		return reply::wrong_type();
	}
	if (*list == nullptr) {
		// With a count the reply is an array, so its nil is the null array.
		return count ? reply::nil_array() : reply::nil();
	}

	// Replies with the element at the end, then drops it.
	auto take_one = [&] {
		auto &l = **list;
		if constexpr (Where == End::Head) {
			Value element = reply::bulk(l.front());
			l.pop_front();
			return element;
		} else {
			Value element = reply::bulk(l.back());
			l.pop_back();
			return element;
		}
	};

	Value result = reply::nil();
	if (!count) {
		result = take_one();
	} else {
		const auto n =
			std::min<size_t>(static_cast<size_t>(*count), (*list)->size());
		Value::Array popped;
		popped.reserve(n);
		for (size_t i = 0; i < n; ++i) popped.push_back(take_one());
		result = reply::array(std::move(popped));
	}
	ctx.db.erase_if_empty(key);
	return result;
}

Value llen(CommandContext &ctx, Args args) {
	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) {
		return reply::wrong_type();
	}
	return reply::integer(((*list) != nullptr) ? static_cast<int64_t>((*list)->size())
									 : 0);
}

// Converts a Redis list index (negative counts from the tail) to an offset.
REDIS_CONST_FN REDIS_HOT_FN int64_t normalize_index(int64_t index,
													int64_t size) {
	return index < 0 ? index + size : index;
}

Value lrange(CommandContext &ctx, Args args) {
	const auto start_arg = parse_integer(args[2]);
	const auto stop_arg  = parse_integer(args[3]);
	if (!start_arg || !stop_arg) {
		return reply::not_integer();
	}

	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) {
		return reply::wrong_type();
	}
	if (*list == nullptr) {
		return reply::array({});
	}

	const auto size  = static_cast<int64_t>((*list)->size());
	const auto start = std::max<int64_t>(normalize_index(*start_arg, size), 0);
	const auto stop  = std::min(normalize_index(*stop_arg, size), size - 1);
	if (start > stop) {
		return reply::array({});
	}

	Value::Array range;
	range.reserve(static_cast<size_t>(stop - start + 1));
	for (auto i = start; i <= stop; ++i)
		range.push_back(reply::bulk((**list)[static_cast<size_t>(i)]));
	return reply::array(std::move(range));
}

Value lindex(CommandContext &ctx, Args args) {
	const auto index_arg = parse_integer(args[2]);
	if (!index_arg) {
		return reply::not_integer();
	}

	const auto list = ctx.db.find_as<List>(args[1]);
	if (!list) {
		return reply::wrong_type();
	}
	if (*list == nullptr) {
		return reply::nil();
	}

	const auto size  = static_cast<int64_t>((*list)->size());
	const auto index = normalize_index(*index_arg, size);
	if (index < 0 || index >= size) {
		return reply::nil();
	}
	return reply::bulk((**list)[static_cast<size_t>(index)]);
}

constexpr std::array COMMANDS{
	CommandInfo{
		.name = "LPUSH", .fn = push<End::Head>, .arity = -3, .write = true},
	CommandInfo{
		.name = "RPUSH", .fn = push<End::Tail>, .arity = -3, .write = true},
	CommandInfo{
		.name = "LPOP", .fn = pop<End::Head>, .arity = -2, .write = true},
	CommandInfo{
		.name = "RPOP", .fn = pop<End::Tail>, .arity = -2, .write = true},
	CommandInfo{.name = "LLEN", .fn = llen, .arity = 2},
	CommandInfo{.name = "LRANGE", .fn = lrange, .arity = 4},
	CommandInfo{.name = "LINDEX", .fn = lindex, .arity = 3},
};
} // namespace

std::span<const CommandInfo> list_commands() { return COMMANDS; }

} // namespace redis
