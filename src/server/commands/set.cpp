// Set commands: SADD, SREM, SCARD, SMEMBERS, SISMEMBER, SINTER, SPOP.
#include "command.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep
#include "common/store/object.hpp"
#include "core/utils/parse_number.hpp"
#include "server/persistence/append_only_file.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <version> // IWYU pragma: keep

namespace redis {
using resp::Value;

namespace {
Value sadd(CommandContext &ctx, Args args) {
	const auto set = ctx.db.find_or_create<Set>(args[1]);
	if (!set) return reply::wrong_type();
	int64_t added = 0;
	for (const std::string_view member : args.subspan(2))
		added += (*set)->emplace(member).second ? 1 : 0;
	return reply::integer(added);
}

Value srem(CommandContext &ctx, Args args) {
	const std::string_view key = args[1];
	const auto set             = ctx.db.find_as<Set>(key);
	if (!set) return reply::wrong_type();
	if (*set == nullptr) return reply::integer(0);

	int64_t removed = 0;
	for (const std::string_view member : args.subspan(2))
#if defined(__cpp_lib_associative_heterogeneous_erasure) &&                    \
	__cpp_lib_associative_heterogeneous_erasure >= 202110L
		removed += static_cast<int64_t>((*set)->erase(member));
#else
		// Fallback find() + erase(it) idiom
		if (auto it = (*set)->find(member); it != (*set)->end()) {
			(*set)->erase(it);
			removed += 1;
		}
#endif
	ctx.db.erase_if_empty(key);
	return reply::integer(removed);
}

Value scard(CommandContext &ctx, Args args) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return reply::wrong_type();
	return reply::integer(
		((*set) != nullptr) ? static_cast<int64_t>((*set)->size()) : 0);
}

Value smembers(CommandContext &ctx, Args args) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return reply::wrong_type();
	if (*set == nullptr) return reply::array({});
	Value::Array members;
	members.reserve((*set)->size());
	for (const auto &member : **set) members.push_back(reply::bulk(member));
	return reply::array(std::move(members));
}

Value sismember(CommandContext &ctx, Args args) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return reply::wrong_type();
	return reply::integer(((*set) != nullptr) && (*set)->contains(args[2]) ? 1
																		   : 0);
}

Value sinter(CommandContext &ctx, Args args) {
	std::vector<const Set *> sets;
	sets.reserve(args.size() - 1);
	bool any_missing = false;
	for (const std::string_view key : args.subspan(1)) {
		const auto set = ctx.db.find_as<Set>(key);
		// Every key is type-checked, as in Redis: a wrong type after a missing
		// key is still an error.
		if (!set) return reply::wrong_type();
		if (*set == nullptr) any_missing = true;
		else sets.push_back(*set);
	}
	// A missing key is an empty set, so the intersection is empty.
	if (any_missing) return reply::array({});

	// Probe from the smallest set: O(min size * number of sets). The array
	// header needs the count, so the members are collected before they are
	// written.
	std::ranges::sort(sets, {}, &Set::size);
	std::vector<std::string_view> result;
	for (const auto &member : *sets.front()) {
		const bool in_all =
			std::all_of(sets.begin() + 1, sets.end(), [&](const Set *s) {
				return s->contains(member);
			});
		if (in_all) result.emplace_back(member);
	}
	Value::Array members;
	members.reserve(result.size());
	for (const std::string_view member : result)
		members.push_back(reply::bulk(member));
	return reply::array(std::move(members));
}

// Picks a random member in O(buckets) worst case without copying the set:
// choose a random non-empty bucket, then a random entry within it.
Set::const_iterator random_member(const Set &set) {
	thread_local std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<size_t> pick_bucket(0,
													  set.bucket_count() - 1);
	size_t bucket = pick_bucket(rng);
	while (set.bucket_size(bucket) == 0)
		bucket = (bucket + 1) % set.bucket_count();

	std::uniform_int_distribution<size_t> pick_entry(0,
													 set.bucket_size(bucket) -
														 1);
	auto local = set.begin(bucket);
	std::advance(local, pick_entry(rng));
	return set.find(*local);
}

// SPOP key [count]
Value spop(CommandContext &ctx, Args args) {
	if (args.size() > 3)
		return reply::error("ERR wrong number of arguments for 'spop' command");
	std::optional<int64_t> count;
	if (args.size() == 3) {
		const auto parsed = parse_integer(args[2]);
		if (!parsed || *parsed < 0)
			return reply::error("ERR value is out of range, must be positive");
		count = *parsed;
	}

	const std::string_view key = args[1];
	const auto set             = ctx.db.find_as<Set>(key);
	if (!set) return reply::wrong_type();
	if (*set == nullptr) return count ? reply::array({}) : reply::nil();

	// The choice is random, so a replay of SPOP would pop other members: the
	// append-only file gets the members that were popped, as SREM.
	std::vector<std::string> popped;
	auto pop_one = [&] {
		const auto node = (*set)->extract(random_member(**set));
		if (ctx.aof != nullptr)
			popped.emplace_back(std::string_view(node.value()));
		return reply::bulk(node.value());
	};

	Value result = reply::nil();
	if (!count) {
		result = pop_one();
	} else {
		const auto n =
			std::min<size_t>(static_cast<size_t>(*count), (*set)->size());
		Value::Array members;
		members.reserve(n);
		for (size_t i = 0; i < n; ++i) members.push_back(pop_one());
		result = reply::array(std::move(members));
	}
	ctx.db.erase_if_empty(key);

	if (!popped.empty() && ctx.aof != nullptr) {
		std::vector<std::string_view> srem{"SREM", key};
		srem.insert(srem.end(), popped.begin(), popped.end());
		ctx.aof->replace(srem);
	}
	return result;
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "SADD", .fn = sadd, .arity = -3, .write = true},
	CommandInfo{.name = "SREM", .fn = srem, .arity = -3, .write = true},
	CommandInfo{.name = "SCARD", .fn = scard, .arity = 2},
	CommandInfo{.name = "SMEMBERS", .fn = smembers, .arity = 2},
	CommandInfo{.name = "SISMEMBER", .fn = sismember, .arity = 3},
	CommandInfo{.name = "SINTER", .fn = sinter, .arity = -2},
	CommandInfo{.name = "SPOP", .fn = spop, .arity = -2, .write = true},
};
} // namespace

std::span<const CommandInfo> set_commands() { return COMMANDS; }

} // namespace redis
