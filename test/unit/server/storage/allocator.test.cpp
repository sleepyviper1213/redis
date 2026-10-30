#include "server/storage/database.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
/// Allocations and frees counted across every copy and rebind of one
/// allocator family, so a test can see what went through it and that
/// everything came back.
struct Ledger {
	std::size_t allocations   = 0;
	std::size_t deallocations = 0;

	[[nodiscard]] bool balanced() const {
		return allocations == deallocations;
	}
};

/// A stateful allocator: instances with different tags are unequal, so the
/// allocator-aware paths have something to decide.
template <typename T>
struct TrackingAllocator {
	using value_type      = T;
	using is_always_equal = std::false_type;

	int tag        = 0;
	Ledger *ledger = nullptr;

	TrackingAllocator(int t, Ledger *l) noexcept : tag(t), ledger(l) {}

	// Implicit, as std::allocator's is: uses-allocator construction hands a
	// container this allocator rebound to another type.
	template <typename U>
	TrackingAllocator( // NOLINT(google-explicit-constructor)
		const TrackingAllocator<U> &other) noexcept
		: tag(other.tag), ledger(other.ledger) {}

	T *allocate(std::size_t n) {
		++ledger->allocations;
		return std::allocator<T>{}.allocate(n);
	}

	void deallocate(T *p, std::size_t n) noexcept {
		++ledger->deallocations;
		std::allocator<T>{}.deallocate(p, n);
	}

	template <typename U>
	bool operator==(const TrackingAllocator<U> &other) const noexcept {
		return tag == other.tag;
	}
};

using Alloc    = TrackingAllocator<char>;
using Object   = redis::BasicObject<Alloc>;
using Database = redis::BasicDatabase<Alloc>;
using String   = Object::String;
using List     = Object::List;
using Hash     = Object::Hash;
} // namespace

SCENARIO("An object takes its storage from its allocator", "[allocator]") {
	static_assert(std::is_nothrow_move_constructible_v<Object>);

	Ledger ledger;

	GIVEN("a list built through a stateful allocator") {
		{
			Object object = Object::make<List>(Alloc{1, &ledger});
			object.as<List>()->push_back(
				String("a long element, past the small-string buffer",
					   Alloc{1, &ledger}));

			THEN("the block and the list's array came from it") {
				CHECK(ledger.allocations >= 2);
				CHECK(object.as<List>()->get_allocator().tag == 1);
			}

			WHEN("it is moved") {
				const auto before = ledger.allocations;
				Object moved(std::move(object));

				THEN("the value is taken over without allocating") {
					CHECK(ledger.allocations == before);
					CHECK(object.as<List>() == nullptr); // NOLINT
					CHECK(moved.as<List>()->size() == 1);
				}
			}

			WHEN("it is moved into an object with an unequal allocator") {
				Object other(std::move(object), Alloc{2, &ledger});

				THEN("the value is rebuilt in storage from the new one") {
					CHECK(other.get_allocator().tag == 2);
					CHECK(other.as<List>()->get_allocator().tag == 2);
					CHECK(other.as<List>()->front() ==
						  "a long element, past the small-string buffer");
					CHECK(object.as<List>() == nullptr); // NOLINT
				}
			}

			WHEN("it is copied") {
				const Object copy(object);

				THEN("the copy is deep and uses the allocator the source "
					 "selects") {
					CHECK(copy.get_allocator().tag == 1);
					CHECK(copy.as<List>() != object.as<List>());
					CHECK(copy.as<List>()->front() == object.as<List>()->front());
				}
			}
		}

		THEN("everything allocated is freed") { CHECK(ledger.balanced()); }
	}
}

SCENARIO("Short strings are embedded in the object", "[allocator]") {
	static_assert(sizeof(redis::Object) == 16);

	Ledger ledger;
	const Alloc alloc{1, &ledger};
	const std::string_view longer = "longer than fourteen bytes";

	GIVEN("a string that fits in the object") {
		{
			Object object = Object::make_string(alloc, "value:12345");

			THEN("it is stored inline, with no allocation") {
				CHECK(object.embedded());
				CHECK(object.string() == "value:12345");
				CHECK(ledger.allocations == 0);
			}

			WHEN("it is copied and moved") {
				Object copy(object);
				Object moved(std::move(copy));

				THEN("the bytes come along, still with no allocation") {
					CHECK(moved.string() == "value:12345");
					CHECK(ledger.allocations == 0);
				}
			}

			WHEN("it is edited in place") {
				object.mutable_string().append(" and more");

				THEN("it moves to the heap first") {
					CHECK_FALSE(object.embedded());
					CHECK(object.string() == "value:12345 and more");
					// The String's block, then its buffer once the append
					// outgrows the small-string capacity.
					CHECK(ledger.allocations == 2);
				}
			}

			WHEN("a longer string is assigned, then a short one again") {
				object.assign_string(longer);
				REQUIRE_FALSE(object.embedded());
				CHECK(object.string() == longer);
				object.assign_string("short");

				THEN("the heap string is freed and the short one embedded") {
					CHECK(object.embedded());
					CHECK(object.string() == "short");
					CHECK(ledger.balanced());
				}
			}
		}

		THEN("everything allocated is freed") { CHECK(ledger.balanced()); }
	}

	GIVEN("a string longer than the object holds") {
		{
			const Object object = Object::make_string(alloc, longer);

			THEN("it lives on the heap") {
				CHECK_FALSE(object.embedded());
				CHECK(object.string() == longer);
				CHECK(ledger.allocations >= 1);
			}
		}

		THEN("everything allocated is freed") { CHECK(ledger.balanced()); }
	}
}

SCENARIO("A database allocates keys, values and tables from one allocator",
		 "[allocator]") {
	Ledger ledger;

	GIVEN("a database with a stateful allocator") {
		{
			Database db(Alloc{7, &ledger});
			REQUIRE(db.get_allocator().tag == 7);

			for (int i = 0; i < 100; ++i) {
				const String key("key:" + std::to_string(i) +
									 ":long enough to allocate",
								 Alloc{7, &ledger});
				db.set(key, key);
			}
			auto hash = db.find_or_create<Hash>("hash");
			REQUIRE(hash.has_value());
			// Elements take the allocator they are given: the map's node
			// allocator does not reach the strings inside the node.
			(*hash)->emplace(String("field", Alloc{7, &ledger}),
							 String("value", Alloc{7, &ledger}));
			db.set_expiry("key:1:long enough to allocate",
						  Database::clock::now() + std::chrono::hours(1));

			THEN("they all went through it") {
				CHECK(ledger.allocations > 200);
				CHECK(db.size() == 101);
				CHECK((*hash)->get_allocator().tag == 7);
			}

			WHEN("keys are erased") {
				db.erase("key:1:long enough to allocate");
				db.erase("hash");

				THEN("the rest stay reachable") {
					CHECK(db.size() == 99);
					CHECK(db.find("key:2:long enough to allocate") != nullptr);
				}
			}
		}

		THEN("everything allocated is freed") { CHECK(ledger.balanced()); }
	}
}
