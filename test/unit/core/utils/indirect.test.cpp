#include "core/utils/indirect.hpp"

#include <catch2/catch_test_macros.hpp>

#include <compare>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using redis::indirect;
using redis::make_indirect;

// What this pins is conformance with [indirect.ctor] and friends, not just
// "does it work". The type is a stand-in for C++26's std::indirect, and a
// vocabulary type that accepts less than the standard one teaches the wrong
// spelling at every call site - while one that accepts *more* compiles code
// that will break when the standard library's own arrives. Both directions are
// checked, which is why several assertions below are static_asserts that
// something is *not* constructible.

namespace {

/// An allocator whose instances are distinguishable, so the allocator-extended
/// operations have something to actually decide. Prefixed for tree-wide
/// uniqueness, as in the exchange_engine tests these were ported from.
template <typename T>
struct indirect_tagged_allocator {
	using value_type = T;
	// Both defaulted the other way round from std::allocator, so the branches
	// that only exist for stateful allocators are reachable from a test.
	using is_always_equal                        = std::false_type;
	using propagate_on_container_copy_assignment = std::true_type;

	int tag = 0;

	indirect_tagged_allocator() = default;

	explicit indirect_tagged_allocator(int t) : tag(t) {}

	template <typename U>
	explicit indirect_tagged_allocator(const indirect_tagged_allocator<U> &o)
		: tag(o.tag) {}

	[[nodiscard]] T *allocate(std::size_t n) {
		return static_cast<T *>(::operator new(n * sizeof(T)));
	}

	void deallocate(T *p, std::size_t /*n*/) noexcept { ::operator delete(p); }

	bool operator==(const indirect_tagged_allocator &o) const noexcept {
		return tag == o.tag;
	}
};

/// Counts its own lifetime events, so a deep copy can be told from a shared
/// pointer by construction count rather than by address comparison.
struct indirect_tracked {
	static int live;
	int value = 0;

	explicit indirect_tracked(int v) : value(v) { ++live; }

	indirect_tracked(const indirect_tracked &o) : value(o.value) { ++live; }

	indirect_tracked(indirect_tracked &&o) noexcept : value(o.value) { ++live; }

	indirect_tracked &operator=(const indirect_tracked &) = default;
	indirect_tracked &operator=(indirect_tracked &&)      = default;

	~indirect_tracked() { --live; }
};

int indirect_tracked::live = 0;

} // namespace

// --- construction, per [indirect.ctor] -------------------------------------

TEST_CASE("default construction value initialises the owned object", "[indirect]") {
	const indirect<int> value;
	CHECK((*value) == (0));
	CHECK_FALSE(value.valueless_after_move());
}

TEST_CASE("constructs from a value without the in place tag", "[indirect]") {
	// The workhorse spelling, and the one whose absence forced every call site
	// to say std::in_place.
	const indirect<std::string> text("hello");
	CHECK((*text) == ("hello"));
}

TEST_CASE("constructs in place from several arguments", "[indirect]") {
	const indirect<std::string> text(std::in_place, 3, 'x');
	CHECK((*text) == ("xxx"));
}

TEST_CASE("constructs in place from a braced list", "[indirect]") {
	// A braced-init-list is a non-deduced context, so no variadic overload can
	// take it - the standard splits out an initializer_list constructor and so
	// does this. Without it this line does not compile at all.
	const indirect<std::vector<int>> numbers(std::in_place, {1, 2, 3});
	REQUIRE((numbers->size()) == (3U));
	CHECK(((*numbers)[2]) == (3));
}

TEST_CASE("every tagged constructor is explicit", "[indirect]") {
	using tagged = indirect<std::string>;
	// Direct-initialisation works; copy-initialisation must not. The
	// allocator-extended in-place constructor was missing its `explicit`,
	// which made the second of these compile.
	static_assert(
		std::is_constructible_v<tagged, std::in_place_t, const char *>);
	static_assert(!std::is_convertible_v<std::in_place_t, tagged>);
	static_assert(std::is_constructible_v<tagged,
										  std::allocator_arg_t,
										  std::allocator<std::string>,
										  std::in_place_t,
										  const char *>);
	// The value constructor is explicit too, so no implicit T -> indirect<T>.
	static_assert(!std::is_convertible_v<std::string, tagged>);
	SUCCEED();
}

TEST_CASE("constraints make the constructors sfinae friendly", "[indirect]") {
	struct needs_int {
		explicit needs_int(int /*unused*/) {}
	};

	// Answers false rather than hard-erroring inside the allocation helper,
	// which is what a Constraint buys over a bare template.
	static_assert(!std::is_constructible_v<indirect<needs_int>,
										   std::in_place_t,
										   const char *>);
	static_assert(
		std::is_constructible_v<indirect<needs_int>, std::in_place_t, int>);
	SUCCEED();
}

TEST_CASE("is usable in constant evaluation", "[indirect]") {
	// Every std::indirect constructor is constexpr; so are these.
	constexpr auto doubled = [] {
		indirect<int> v(std::in_place, 21);
		return *v * 2;
	}();
	static_assert(doubled == 42);
	CHECK((doubled) == (42));
}

// --- value semantics -------------------------------------------------------

TEST_CASE("copying constructs a second object rather than sharing one", "[indirect]") {
	REQUIRE((indirect_tracked::live) == (0));
	{
		indirect<indirect_tracked> first(std::in_place, 7);
		REQUIRE((indirect_tracked::live) == (1));

		const indirect<indirect_tracked> second(first);
		CHECK((indirect_tracked::live) == (2)); // "copy shared the object"
		CHECK((second->value) == (7));

		// Writing through one must not be visible through the other - the whole
		// reason this type exists rather than a shared_ptr.
		first->value = 9;
		CHECK((second->value) == (7));
	}
	CHECK((indirect_tracked::live) == (0)); // "an owned object leaked"
}

TEST_CASE("moving leaves the source valueless", "[indirect]") {
	indirect<std::string> from(std::in_place, "payload");
	const indirect<std::string> to(std::move(from));

	CHECK(from.valueless_after_move());
	CHECK((*to) == ("payload"));
}

TEST_CASE("move construction is nothrow so containers can rely on it", "[indirect]") {
	static_assert(std::is_nothrow_move_constructible_v<indirect<std::string>>);
	SUCCEED();
}

TEST_CASE("copy assignment reuses the owned object when there is one", "[indirect]") {
	REQUIRE((indirect_tracked::live) == (0));
	{
		indirect<indirect_tracked> target(std::in_place, 1);
		const indirect<indirect_tracked> source(std::in_place, 2);
		REQUIRE((indirect_tracked::live) == (2));

		target = source;
		// Assigned into, not reallocated: still two objects, not three.
		CHECK((indirect_tracked::live) == (2));
		CHECK((target->value) == (2));
	}
	CHECK((indirect_tracked::live) == (0));
}

TEST_CASE("assigning over a valueless target allocates again", "[indirect]") {
	indirect<std::string> target(std::in_place, "first");
	const indirect<std::string> source(std::in_place, "second");

	const indirect<std::string> stolen(std::move(target));
	REQUIRE(target.valueless_after_move());

	target = source;
	REQUIRE_FALSE(target.valueless_after_move());
	CHECK((*target) == ("second"));
}

TEST_CASE("self assignment leaves the value alone", "[indirect]") {
	indirect<std::string> value(std::in_place, "kept");
	const indirect<std::string> *alias = &value;

	value = *alias;
	REQUIRE_FALSE(value.valueless_after_move());
	CHECK((*value) == ("kept"));
}

// --- const correctness -----------------------------------------------------

TEST_CASE("const access yields const access to the value", "[indirect]") {
	// A deduced-this operator* dereferenced a `T *const` and handed out a
	// mutable T& from a const indirect. These pin that it does not.
	static_assert(
		std::is_same_v<decltype(*std::declval<indirect<int> &>()), int &>);
	static_assert(
		std::is_same_v<decltype(*std::declval<const indirect<int> &>()),
					   const int &>);
	static_assert(std::is_same_v<
				  decltype(std::declval<const indirect<int> &>().operator->()),
				  const int *>);
	SUCCEED();
}

// --- allocator handling ----------------------------------------------------

TEST_CASE("an allocator extended constructor uses the allocator it is given", "[indirect]") {
	using alloc = indirect_tagged_allocator<std::string>;
	const indirect<std::string, alloc> value(std::allocator_arg,
											 alloc{42},
											 std::in_place,
											 "text");
	CHECK((value.get_allocator().tag) == (42));
	CHECK((*value) == ("text"));
}

TEST_CASE("an allocator extended move with an unequal allocator moves the value", "[indirect]") {
	using alloc = indirect_tagged_allocator<std::string>;
	indirect<std::string, alloc> from(std::allocator_arg,
									  alloc{1},
									  std::in_place,
									  "payload");

	// Different tag, so the pointer cannot be adopted and the value has to be
	// rebuilt in our own storage - but `from` must still end up valueless,
	// which is the postcondition the standard states for both paths.
	const indirect<std::string, alloc> to(std::allocator_arg,
										  alloc{2},
										  std::move(from));

	CHECK(from.valueless_after_move());
	CHECK((to.get_allocator().tag) == (2));
	CHECK((*to) == ("payload"));
}

TEST_CASE("an allocator extended move with an equal allocator adopts the pointer", "[indirect]") {
	using alloc = indirect_tagged_allocator<indirect_tracked>;
	REQUIRE((indirect_tracked::live) == (0));
	{
		indirect<indirect_tracked, alloc> from(std::allocator_arg,
											   alloc{5},
											   std::in_place,
											   3);
		REQUIRE((indirect_tracked::live) == (1));

		const indirect<indirect_tracked, alloc> to(std::allocator_arg,
												   alloc{5},
												   std::move(from));
		// Adopted, so no second object was ever built.
		CHECK((indirect_tracked::live) == (1));
		CHECK(from.valueless_after_move());
		CHECK((to->value) == (3));
	}
	CHECK((indirect_tracked::live) == (0));
}

// --- swap, comparison, hash ------------------------------------------------

TEST_CASE("swap exchanges the owned objects without allocating", "[indirect]") {
	REQUIRE((indirect_tracked::live) == (0));
	{
		indirect<indirect_tracked> left(std::in_place, 1);
		indirect<indirect_tracked> right(std::in_place, 2);
		REQUIRE((indirect_tracked::live) == (2));

		swap(left, right);
		CHECK((indirect_tracked::live) == (2)); // "swap built a new object"
		CHECK((left->value) == (2));
		CHECK((right->value) == (1));
	}
	CHECK((indirect_tracked::live) == (0));
}

TEST_CASE("comparison reads through to the value", "[indirect]") {
	const indirect<int> small(std::in_place, 1);
	const indirect<int> large(std::in_place, 2);
	const indirect<int> also_small(std::in_place, 1);

	CHECK((small) == (also_small));
	CHECK((small) != (large));
	CHECK((small) < (large));
	CHECK((large) > (small));
}

TEST_CASE("two valueless operands compare equal and order before a value", "[indirect]") {
	indirect<int> emptied(std::in_place, 1);
	indirect<int> also_emptied(std::in_place, 2);
	const indirect<int> held(std::in_place, 0);

	const indirect<int> sink_one(std::move(emptied));
	const indirect<int> sink_two(std::move(also_emptied));
	REQUIRE(emptied.valueless_after_move());
	REQUIRE(also_emptied.valueless_after_move());

	// Comparing a valueless operand is answerable here rather than undefined:
	// the flags are compared instead of the values, so a container of these
	// stays usable after a move rather than reading through a null.
	CHECK((emptied) == (also_emptied));
	CHECK((emptied) != (held));

	// The direction is asserted from both ends and on the ordering itself,
	// because getting it backwards is a passing test away from invisible:
	// [indirect.relops] says `!lhs.valueless <=> !rhs.valueless`, and dropping
	// that negation compares the flags directly - which inverts the answer,
	// since `true > false`. That is the bug this line caught.
	CHECK((emptied) < (held));
	CHECK((held) > (emptied));
	CHECK((emptied <=> held) == (std::strong_ordering::less));
	CHECK((held <=> emptied) == (std::strong_ordering::greater));
	CHECK((emptied <=> also_emptied) == (std::strong_ordering::equal));
}

TEST_CASE("hashing agrees with the owned values hash", "[indirect]") {
	const indirect<std::string> value(std::in_place, "key");
	CHECK((std::hash<indirect<std::string>>{}(value)) == (std::hash<std::string>{}("key")));
}

TEST_CASE("make indirect forwards to the in place constructor", "[indirect]") {
	const auto text = make_indirect<std::string>(4, 'z');
	CHECK((*text) == ("zzzz"));
}
