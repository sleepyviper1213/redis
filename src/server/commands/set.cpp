// Set commands: SADD, SREM, SCARD, SMEMBERS, SISMEMBER, SINTER, SPOP.
#include "command.hpp"
#include "server/storage/database.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <vector>

namespace redis {
using resp::Value;

namespace {
Value members_reply(const Set &set) {
	Value::Array members;
	members.reserve(set.size());
	for (const auto &member : set) members.push_back(reply::bulk(member));
	return Value::from_array(std::move(members));
}

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
	const auto set         = ctx.db.find_as<Set>(key);
	if (!set) return reply::wrong_type();
	if (*set == nullptr) return reply::integer(0);

	int64_t removed = 0;
	for (const std::string_view member : args.subspan(2))
		removed += static_cast<int64_t>((*set)->erase(member));
	ctx.db.erase_if_empty(key);
	return reply::integer(removed);
}

Value scard(CommandContext &ctx, Args args) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return reply::wrong_type();
	return reply::integer(*set ? static_cast<int64_t>((*set)->size()) : 0);
}

Value smembers(CommandContext &ctx, Args args) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return reply::wrong_type();
	return *set ? members_reply(**set) : Value::from_array({});
}

Value sismember(CommandContext &ctx, Args args) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return reply::wrong_type();
	return reply::integer(*set && (*set)->contains(args[2]) ? 1 : 0);
}

Value sinter(CommandContext &ctx, Args args) {
	std::vector<const Set *> sets;
	sets.reserve(args.size() - 1);
	for (const std::string_view key : args.subspan(1)) {
		const auto set = ctx.db.find_as<Set>(key);
		if (!set) return reply::wrong_type();
		// A missing key is an empty set, so the intersection is empty.
		if (*set == nullptr) return Value::from_array({});
		sets.push_back(*set);
	}

	// Probe from the smallest set: O(min size * number of sets).
	std::ranges::sort(sets, {}, &Set::size);
	Value::Array result;
	for (const auto &member : *sets.front()) {
		const bool in_all = std::all_of(sets.begin() + 1,
										sets.end(),
										[&](const Set *s) {
											return s->contains(member);
										});
		if (in_all) result.push_back(reply::bulk(member));
	}
	return Value::from_array(std::move(result));
}

// Picks a random member in O(buckets) worst case without copying the set:
// choose a random non-empty bucket, then a random entry within it.
Set::const_iterator random_member(const Set &set) {
	thread_local std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<size_t> pick_bucket(0, set.bucket_count() - 1);
	size_t bucket = pick_bucket(rng);
	while (set.bucket_size(bucket) == 0)
		bucket = (bucket + 1) % set.bucket_count();

	std::uniform_int_distribution<size_t> pick_entry(0,
													 set.bucket_size(bucket) - 1);
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
		count = parse_int(args[2]);
		if (!count || *count < 0)
			return reply::error("ERR value is out of range, must be positive");
	}

	const std::string_view key = args[1];
	const auto set         = ctx.db.find_as<Set>(key);
	if (!set) return reply::wrong_type();
	if (*set == nullptr) return count ? Value::from_array({}) : reply::nil();

	auto pop_one = [&] {
		auto node = (*set)->extract(random_member(**set));
		return reply::bulk(std::move(node.value()));
	};

	Value result = reply::nil();
	if (!count) {
		result = pop_one();
	} else {
		const auto n =
			std::min<size_t>(static_cast<size_t>(*count), (*set)->size());
		Value::Array popped;
		popped.reserve(n);
		for (size_t i = 0; i < n; ++i) popped.push_back(pop_one());
		result = Value::from_array(std::move(popped));
	}
	ctx.db.erase_if_empty(key);
	return result;
}

constexpr std::array COMMANDS{
	CommandInfo{"SADD", sadd, -3},
	CommandInfo{"SREM", srem, -3},
	CommandInfo{"SCARD", scard, 2},
	CommandInfo{"SMEMBERS", smembers, 2},
	CommandInfo{"SISMEMBER", sismember, 3},
	CommandInfo{"SINTER", sinter, -2},
	CommandInfo{"SPOP", spop, -2},
};
} // namespace

std::span<const CommandInfo> set_commands() { return COMMANDS; }

} // namespace redis
