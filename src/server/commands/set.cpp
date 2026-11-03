// Set commands: SADD, SREM, SCARD, SMEMBERS, SISMEMBER, SINTER, SPOP.
#include "command.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <optional>
#include <random>
#include <vector>
#include <version>

namespace redis {

namespace {
void sadd(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto set = ctx.db.find_or_create<Set>(args[1]);
	if (!set) return out.wrong_type();
	int64_t added = 0;
	for (const std::string_view member : args.subspan(2))
		added += (*set)->emplace(member).second ? 1 : 0;
	out.integer(added);
}

void srem(CommandContext &ctx, Args args, ReplyWriter &out) {
	const std::string_view key = args[1];
	const auto set             = ctx.db.find_as<Set>(key);
	if (!set) return out.wrong_type();
	if (*set == nullptr) return out.integer(0);

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
	out.integer(removed);
}

void scard(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return out.wrong_type();
	out.integer(*set ? static_cast<int64_t>((*set)->size()) : 0);
}

void smembers(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return out.wrong_type();
	if (*set == nullptr) return out.array(0);
	out.array((*set)->size());
	for (const auto &member : **set) out.bulk(member);
}

void sismember(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto set = ctx.db.find_as<Set>(args[1]);
	if (!set) return out.wrong_type();
	out.integer(*set && (*set)->contains(args[2]) ? 1 : 0);
}

void sinter(CommandContext &ctx, Args args, ReplyWriter &out) {
	std::vector<const Set *> sets;
	sets.reserve(args.size() - 1);
	for (const std::string_view key : args.subspan(1)) {
		const auto set = ctx.db.find_as<Set>(key);
		if (!set) return out.wrong_type();
		// A missing key is an empty set, so the intersection is empty.
		if (*set == nullptr) return out.array(0);
		sets.push_back(*set);
	}

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
	out.array(result.size());
	for (const std::string_view member : result) out.bulk(member);
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
void spop(CommandContext &ctx, Args args, ReplyWriter &out) {
	if (args.size() > 3)
		return out.error("ERR wrong number of arguments for 'spop' command");
	std::optional<int64_t> count;
	if (args.size() == 3) {
		count = parse_int(args[2]);
		if (!count || *count < 0)
			return out.error("ERR value is out of range, must be positive");
	}

	const std::string_view key = args[1];
	const auto set             = ctx.db.find_as<Set>(key);
	if (!set) return out.wrong_type();
	if (*set == nullptr) return count ? out.array(0) : out.nil();

	auto pop_one = [&] {
		const auto node = (*set)->extract(random_member(**set));
		out.bulk(node.value());
	};

	if (!count) {
		pop_one();
	} else {
		const auto n =
			std::min<size_t>(static_cast<size_t>(*count), (*set)->size());
		out.array(n);
		for (size_t i = 0; i < n; ++i) pop_one();
	}
	ctx.db.erase_if_empty(key);
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
