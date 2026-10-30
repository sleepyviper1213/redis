#include "server/storage/simple_dynamic_string.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
/// Records every allocation's size, so a test can check the header chosen.
struct Sizes {
	std::size_t last       = 0;
	std::size_t live_bytes = 0;
};

template <typename T>
struct SizingAllocator {
	using value_type = T;

	Sizes *sizes;

	explicit SizingAllocator(Sizes *s) noexcept : sizes(s) {}
	template <typename U>
	SizingAllocator( // NOLINT(google-explicit-constructor)
		const SizingAllocator<U> &other) noexcept
		: sizes(other.sizes) {}

	T *allocate(std::size_t n) {
		sizes->last = n;
		sizes->live_bytes += n;
		return std::allocator<T>{}.allocate(n);
	}
	void deallocate(T *p, std::size_t n) noexcept {
		sizes->live_bytes -= n;
		std::allocator<T>{}.deallocate(p, n);
	}

	template <typename U>
	bool operator==(const SizingAllocator<U> &other) const noexcept {
		return sizes == other.sizes;
	}
};

using Sds = redis::BasicSimpleDynamicString<SizingAllocator<char>>;
} // namespace

SCENARIO("An sds is one pointer to header, bytes and NUL", "[sds]") {
	static_assert(sizeof(redis::BasicSimpleDynamicString<>) == sizeof(char *));
	static_assert(std::is_nothrow_move_constructible_v<redis::BasicSimpleDynamicString<>>);

	Sizes sizes;
	const SizingAllocator<char> alloc(&sizes);

	GIVEN("strings at each of Redis's header boundaries") {
		THEN("each takes the smallest header that holds its length") {
			// type 5 below 32 bytes, then 8, 16 and 32 bits of len and alloc.
			const std::pair<std::size_t, std::size_t> cases[] = {
				{0, 1},	 {16, 1},	 {31, 1},	 {32, 3},
				{255, 3}, {256, 5}, {65535, 5}, {65536, 9},
			};
			for (const auto &[length, header] : cases) {
				const std::string text(length, 'x');
				{
					const Sds sds(text, alloc);
					CHECK(sds.view() == text);
					CHECK(sds.size() == length);
					CHECK(sizes.last == header + length + 1);
					CHECK(Sds::header_size(length) == header);
				}
				CHECK(sizes.live_bytes == 0);
			}
		}
	}

	GIVEN("a key") {
		const Sds key("key:000000000001", alloc);

		THEN("it reads as a NUL-terminated string_view and compares by value") {
			CHECK(std::string_view(key.c_str()) == "key:000000000001");
			CHECK(key == std::string_view("key:000000000001"));
			CHECK(key < std::string_view("key:000000000002"));
			CHECK(sizes.last == 18); // 1 header + 16 bytes + NUL
		}

		WHEN("it is copied and moved") {
			Sds copy(key);
			Sds moved(std::move(copy));

			THEN("the copy is independent and the move takes the bytes") {
				CHECK(moved == key);
				CHECK(moved.c_str() != key.c_str());
				CHECK(copy.empty()); // NOLINT(bugprone-use-after-move)
			}
		}
	}
}
