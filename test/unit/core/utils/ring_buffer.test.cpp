#include "core/utils/ring_buffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using redis::RingBuffer;

namespace {
template <typename T, typename Alloc>
std::vector<T> contents(const RingBuffer<T, Alloc> &buffer) {
	return {buffer.begin(), buffer.end()};
}
} // namespace

SCENARIO("Pushing and popping at both ends", "[ring_buffer]") {
	GIVEN("an empty buffer") {
		RingBuffer<int> buffer;
		REQUIRE(buffer.empty());

		WHEN("elements are pushed at the back and the front") {
			buffer.push_back(2);
			buffer.push_back(3);
			buffer.push_front(1);
			buffer.push_front(0);

			THEN("they read in deque order") {
				CHECK(contents(buffer) == std::vector{0, 1, 2, 3});
				CHECK(buffer.front() == 0);
				CHECK(buffer.back() == 3);
				CHECK(buffer[2] == 2);
			}

			AND_WHEN("one is popped from each end") {
				buffer.pop_front();
				buffer.pop_back();

				THEN("the middle remains") {
					CHECK(contents(buffer) == std::vector{1, 2});
				}
			}
		}
	}
}

SCENARIO("Growing while the contents wrap around", "[ring_buffer]") {
	GIVEN("a full buffer whose start is not at index 0") {
		RingBuffer<std::string> buffer;
		for (int i = 0; i < 4; ++i) buffer.push_back(std::to_string(i));
		buffer.pop_front();
		buffer.push_back("4"); // wraps into the slot pop_front freed
		REQUIRE(buffer.size() == buffer.capacity());

		WHEN("one more element forces growth") {
			buffer.push_front("0");

			THEN("order survives the move to the larger array") {
				CHECK(buffer.capacity() > 4);
				CHECK(contents(buffer) ==
					  std::vector<std::string>{"0", "1", "2", "3", "4"});
			}
		}
	}

	GIVEN("many pushes at the front") {
		RingBuffer<int> buffer;
		for (int i = 0; i < 1000; ++i) buffer.push_front(i);

		THEN("indexing counts from the most recent") {
			REQUIRE(buffer.size() == 1000);
			CHECK(buffer[0] == 999);
			CHECK(buffer[999] == 0);
		}
	}
}

SCENARIO("Copying and moving", "[ring_buffer]") {
	static_assert(std::is_nothrow_move_constructible_v<RingBuffer<std::string>>,
				  "the reason this type exists: hash tables move it on rehash");
	static_assert(sizeof(RingBuffer<std::string>) == 32);

	GIVEN("a buffer holding strings") {
		RingBuffer<std::string> original;
		original.push_back("a");
		original.push_front("b");

		WHEN("it is copied") {
			RingBuffer<std::string> copy(original);
			copy.push_back("c");

			THEN("the copy is independent") {
				CHECK(contents(original) == std::vector<std::string>{"b", "a"});
				CHECK(contents(copy) ==
					  std::vector<std::string>{"b", "a", "c"});
			}
		}

		WHEN("it is moved") {
			RingBuffer<std::string> moved(std::move(original));

			THEN("the elements move and the source is left empty") {
				CHECK(contents(moved) == std::vector<std::string>{"b", "a"});
				CHECK(original.empty()); // NOLINT(bugprone-use-after-move)
				CHECK(original.capacity() == 0);
			}

			AND_THEN("the empty source is still usable") {
				original.push_back("again");
				CHECK(original.front() == "again");
			}
		}
	}
}

SCENARIO("Constructing in place and clearing", "[ring_buffer]") {
	GIVEN("a buffer") {
		RingBuffer<std::string> buffer;

		THEN("emplace_back returns the new element, for in-place filling") {
			std::string &added = buffer.emplace_back(3, 'x');
			added += "!";
			CHECK(buffer.back() == "xxx!");
		}

		WHEN("it is cleared") {
			buffer.push_back("gone");
			const auto capacity = buffer.capacity();
			buffer.clear();

			THEN("it is empty but keeps its array") {
				CHECK(buffer.empty());
				CHECK(buffer.capacity() == capacity);
			}
		}
	}
}

namespace {
/// An allocator that counts what it hands out and whose instances are
/// distinguishable, so the allocator-aware paths have something to decide.
template <typename T>
struct CountingAllocator {
	using value_type      = T;
	using is_always_equal = std::false_type;

	int tag           = 0;
	int *allocations  = nullptr;

	CountingAllocator(int t, int *counter) : tag(t), allocations(counter) {}
	template <typename U>
	explicit CountingAllocator(const CountingAllocator<U> &o)
		: tag(o.tag), allocations(o.allocations) {}

	T *allocate(std::size_t n) {
		++*allocations;
		return std::allocator<T>{}.allocate(n);
	}
	void deallocate(T *p, std::size_t n) noexcept {
		std::allocator<T>{}.deallocate(p, n);
	}

	bool operator==(const CountingAllocator &o) const noexcept {
		return tag == o.tag;
	}
};
} // namespace

SCENARIO("Using a custom allocator", "[ring_buffer]") {
	static_assert(sizeof(RingBuffer<int>) == 32,
				  "an empty allocator must take no space");

	int allocations = 0;
	using Alloc     = CountingAllocator<int>;

	GIVEN("a buffer with a stateful allocator") {
		RingBuffer<int, Alloc> buffer(Alloc{1, &allocations});
		for (int i = 0; i < 5; ++i) buffer.push_back(i);

		THEN("its arrays come from that allocator") {
			CHECK(allocations == 2); // capacity 4, then 8
			CHECK(buffer.get_allocator().tag == 1);
		}

		WHEN("it is moved into a buffer with an unequal allocator") {
			RingBuffer<int, Alloc> other(std::move(buffer),
										 Alloc{2, &allocations});

			THEN("the elements are moved into storage from the new one") {
				CHECK(other.get_allocator().tag == 2);
				CHECK(contents(other) == std::vector{0, 1, 2, 3, 4});
				CHECK(buffer.empty()); // NOLINT(bugprone-use-after-move)
				CHECK(allocations == 3);
			}
		}

		WHEN("it is moved into a buffer with an equal allocator") {
			RingBuffer<int, Alloc> other(std::move(buffer),
										 Alloc{1, &allocations});

			THEN("the array is taken over without allocating") {
				CHECK(contents(other) == std::vector{0, 1, 2, 3, 4});
				CHECK(allocations == 2);
			}
		}

		WHEN("it is copied") {
			const RingBuffer<int, Alloc> copy(buffer);

			THEN("the copy uses the allocator the source selects") {
				CHECK(copy.get_allocator().tag == 1);
				CHECK(contents(copy) == contents(buffer));
			}
		}
	}
}
