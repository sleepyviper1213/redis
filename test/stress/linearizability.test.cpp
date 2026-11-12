// The stress test's linearizability checker, on hand-made histories: it must
// accept what a correct server can answer and reject what it cannot, or a
// passing stress run would mean nothing.
#include "linearizability.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {
using lin::ABSENT;
using lin::Op;
using lin::OpKind;
using lin::Segment;
using lin::ValueId;
using Values = std::vector<ValueId>;

Op set(ValueId v, std::int64_t sent, std::int64_t completed) {
	return {.kind = OpKind::Set, .value = v, .sent = sent, .completed = completed};
}

Op get(ValueId v, std::int64_t sent, std::int64_t completed) {
	return {.kind = OpKind::Get, .value = v, .sent = sent, .completed = completed};
}

Op del(bool removed, std::int64_t sent, std::int64_t completed) {
	return {.kind      = OpKind::Del,
			.removed   = removed,
			.sent      = sent,
			.completed = completed};
}
} // namespace

TEST_CASE("one client's sequential history ends in its last write",
		  "[stress][linearizability]") {
	const Segment s{{{set(1, 0, 1), get(1, 2, 3), del(true, 4, 5),
					  get(ABSENT, 6, 7), set(2, 8, 9)}}};
	CHECK(lin::check({ABSENT}, s) == Values{2});
}

TEST_CASE("a read after a completed write must see it",
		  "[stress][linearizability]") {
	const Segment s{{{set(1, 0, 10)}, {get(ABSENT, 20, 30)}}};
	CHECK(lin::check({ABSENT}, s).empty());
}

TEST_CASE("a read overlapping a write may see either value",
		  "[stress][linearizability]") {
	CHECK(lin::check({ABSENT}, {{{set(1, 0, 10)}, {get(ABSENT, 5, 15)}}}) ==
		  Values{1});
	CHECK(lin::check({ABSENT}, {{{set(1, 0, 10)}, {get(1, 5, 15)}}}) ==
		  Values{1});
}

TEST_CASE("concurrent writes leave either value", "[stress][linearizability]") {
	const Segment s{{{set(1, 0, 10)}, {set(2, 0, 10)}}};
	CHECK(lin::check({ABSENT}, s) == Values{1, 2});
}

TEST_CASE("placing reads early loses no end value",
		  "[stress][linearizability]") {
	// The read of 1 fits SET 1, GET, SET 2 (ends in 2) and SET 2, SET 1, GET
	// (ends in 1). The search places the read as soon as it matches, which
	// must not cost it the second order.
	const Segment s{{{set(1, 0, 10)}, {set(2, 0, 10)}, {get(1, 0, 10)}}};
	CHECK(lin::check({ABSENT}, s) == Values{1, 2});
	// A DEL that removed nothing is a read of "absent".
	const Segment d{{{set(1, 0, 10)}, {del(false, 0, 10)}}};
	CHECK(lin::check({ABSENT}, d) == Values{1});
	CHECK(lin::check({ABSENT}, {{{set(1, 0, 10)}, {del(true, 0, 10)}}}) ==
		  Values{ABSENT});
}

TEST_CASE("a pipeline executes in the order it was sent",
		  "[stress][linearizability]") {
	// All sent at once, so real time orders nothing: only the connection's
	// order says the GET comes after the SET.
	const Segment s{{{set(1, 0, 10), get(ABSENT, 0, 11)}}};
	CHECK(lin::check({ABSENT}, s).empty());
}

TEST_CASE("reads must not go back in time", "[stress][linearizability]") {
	// The writer's two SETs are ordered; a reader that saw 2 cannot see 1 next.
	const Segment s{{{set(1, 0, 1), set(2, 2, 3)},
					 {get(2, 0, 10), get(1, 11, 12)}}};
	CHECK(lin::check({ABSENT}, s).empty());
}

TEST_CASE("DEL's reply must match the key's existence",
		  "[stress][linearizability]") {
	CHECK(lin::check({ABSENT}, {{{del(true, 0, 1)}}}).empty());
	CHECK(lin::check({ABSENT}, {{{del(false, 0, 1)}}}) == Values{ABSENT});
	CHECK(lin::check({5}, {{{del(false, 0, 1)}}}).empty());
	// Two racing DELs of an existing key: exactly one removes it.
	CHECK(lin::check({5}, {{{del(true, 0, 5)}, {del(true, 0, 5)}}}).empty());
	CHECK(lin::check({5}, {{{del(true, 0, 5)}, {del(false, 0, 5)}}}) ==
		  Values{ABSENT});
}

TEST_CASE("a segment starts from every value the last one left",
		  "[stress][linearizability]") {
	const Segment read2{{{get(2, 0, 1)}}};
	CHECK(lin::check({1, 2}, read2) == Values{2});
	CHECK(lin::check({1}, read2).empty());
}

TEST_CASE("a read of a value written later is rejected",
		  "[stress][linearizability]") {
	const Segment s{{{get(1, 0, 1)}, {set(1, 5, 6)}}};
	CHECK(lin::check({ABSENT}, s).empty());
}
