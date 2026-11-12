#pragma once
// Linearizability of one register (a key under SET, DEL and GET), checked by
// search.
//
// The history is cut into segments at points where no request is in flight
// (the stress test makes them with a barrier), so each search is small: a
// segment is checked from every value the key could hold when it began, and
// gives every value it could hold when it ended. A segment with no possible
// end value is not linearizable.
//
// Within a segment an order is valid when it keeps
//   - each client's requests in the order it sent them (a connection executes
//     in order, pipelined or not), and
//   - real time: a request that completed before another was sent comes first.
// and every reply in it is what the register would answer at that point. The
// search walks the orders depth first, one client's next request at a time,
// and remembers every (progress, value) it has been in, so no state is
// explored twice.
//
// Times are taken by the client: the send time before sending, the completion
// time after the reply was read. Both err on the side of overlap, so a real
// ordering is never imposed that the server did not see.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lin {

/// A value a SET wrote. Every SET writes a value of its own, so a GET's reply
/// names the SET it read from.
using ValueId = std::uint64_t;
/// The key does not exist.
inline constexpr ValueId ABSENT = 0;

enum class OpKind : std::uint8_t { Set, Del, Get };

/// One request and what the server replied. A SET is assumed to have replied
/// OK (the caller checks that).
struct Op {
	OpKind kind = OpKind::Get;
	/// Set: the value written. Get: the value read (ABSENT for nil).
	ValueId value = ABSENT;
	/// Del: the reply, whether a key was removed.
	bool removed = false;
	/// Nanoseconds on one steady clock.
	std::int64_t sent      = 0;
	std::int64_t completed = 0;
};

/// The requests of one segment, per client, each client's in the order sent.
struct Segment {
	std::vector<std::vector<Op>> clients;
};

/// The most clients, and requests per client, in one segment: the search's
/// progress is packed into one word.
inline constexpr std::size_t MAX_CLIENTS = 12;
inline constexpr std::size_t MAX_OPS     = 15;

/**
 * \brief Every value the key may hold after \p segment, if it held one of
 * \p before when the segment began. Empty: no order explains the replies.
 *
 * The result is sorted. \p segment must stay within MAX_CLIENTS clients of
 * at most MAX_OPS requests each.
 */
inline std::vector<ValueId> check(const std::vector<ValueId> &before,
								  const Segment &segment) {
	const auto &clients = segment.clients;
	const std::size_t n = clients.size();

	// A state is packed into one word: four bits of progress per client, then
	// a local index for the value. Every value the key can hold during the segment gets the next free
	// index; a value seen again keeps its first one. state() reads them back.
	std::unordered_map<ValueId, std::uint64_t> local;
	const auto add_local_index = [&local](ValueId v) {
		local.try_emplace(v, local.size());
	};
	add_local_index(ABSENT);
	for (const ValueId v : before) add_local_index(v);
	for (const auto &ops : clients)
		for (const Op &op : ops)
			if (op.kind == OpKind::Set) add_local_index(op.value);

	std::vector<std::size_t> pos(n, 0);
	const auto state = [&](ValueId value) {
		std::uint64_t key = 0;
		for (const std::size_t p : pos) key = (key << 4) | p;
		return (key << 16) | local.at(value);
	};

	std::unordered_set<std::uint64_t> seen;
	std::vector<ValueId> after;

	// Real time: no other client's pending request may have completed before
	// client c's next one was sent. A client's later requests complete after
	// its next one, so the next one is all there is to check.
	const auto enabled = [&](std::size_t c) {
		const Op &op = clients[c][pos[c]];
		for (std::size_t other = 0; other < n; ++other)
			if (other != c && pos[other] < clients[other].size() &&
				clients[other][pos[other]].completed < op.sent)
				return false;
		return true;
	};

	// A request that leaves the value as it is and whose reply matches it: a
	// GET of the current value, or a DEL that removed nothing from an absent
	// key.
	const auto reads = [](const Op &op, ValueId value) {
		return (op.kind == OpKind::Get && op.value == value) ||
			   (op.kind == OpKind::Del && !op.removed && value == ABSENT);
	};

	// Depth first over the orders; returns nothing, fills `after`.
	const auto search = [&](auto &self, ValueId value) -> void {
		if (!seen.insert(state(value)).second) return;

		// An enabled read that matches goes first, with no other branch: if
		// any order completes from here, the same order with that read moved
		// to the front does too. The read changes nothing; every request it
		// overtakes was pending, and none of those completed before it was
		// sent (it is enabled); and overtaking only removes constraints from
		// the requests it passes. This is what keeps the search small: reads
		// never branch.
		for (std::size_t c = 0; c < n; ++c)
			if (pos[c] < clients[c].size() && reads(clients[c][pos[c]], value) &&
				enabled(c)) {
				++pos[c];
				self(self, value);
				--pos[c];
				return;
			}

		bool finished = true;
		for (std::size_t c = 0; c < n; ++c) {
			if (pos[c] == clients[c].size()) continue;
			finished     = false;
			const Op &op = clients[c][pos[c]];
			if (!enabled(c)) continue;

			ValueId next = value;
			switch (op.kind) {
			case OpKind::Set: next = op.value; break;
			case OpKind::Del:
				if (op.removed != (value != ABSENT)) continue;
				next = ABSENT;
				break;
			case OpKind::Get:
				if (op.value != value) continue;
				break;
			}
			++pos[c];
			self(self, next);
			--pos[c];
		}
		if (finished) after.push_back(value);
	};

	for (const ValueId v : before) search(search, v);
	std::ranges::sort(after);
	after.erase(std::ranges::unique(after).begin(), after.end());
	return after;
}

} // namespace lin
