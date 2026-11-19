// Built only with REDIS_USE_JEMALLOC.
#include "core/jemalloc_allocator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

using redis::JemallocAllocator;

namespace {
struct alignas(64) CacheLine {
	char bytes[64];
};
} // namespace

// TC-271
SCENARIO("JemallocAllocator is a standard allocator", "[allocator][jemalloc]") {
	GIVEN("allocators of different types") {
		const JemallocAllocator<int> ints;
		const JemallocAllocator<char> chars;

		THEN("they compare equal, and a rebind converts") {
			CHECK(ints == JemallocAllocator<int>{});
			CHECK(ints == chars);
			const JemallocAllocator<long> longs(ints);
			CHECK(longs == ints);
		}
	}

	GIVEN("containers over it") {
		using Text = std::basic_string<char, std::char_traits<char>,
									   JemallocAllocator<char>>;
		std::vector<Text, JemallocAllocator<Text>> texts;
		std::map<int, Text, std::less<>,
				 JemallocAllocator<std::pair<const int, Text>>>
			map;

		WHEN("they grow") {
			for (int i = 0; i < 1000; ++i) {
				texts.emplace_back(std::string(static_cast<std::size_t>(i), 'x'));
				map.emplace(i, Text(100, 'y'));
			}

			THEN("they hold what was put in") {
				CHECK(texts.size() == 1000);
				CHECK(texts[999].size() == 999);
				CHECK(map.at(500) == Text(100, 'y'));
			}
		}
	}

	GIVEN("a type that needs more than the usual alignment") {
		JemallocAllocator<CacheLine> allocator;

		WHEN("a block is allocated") {
			CacheLine *block = allocator.allocate(3);

			THEN("it is aligned, and can be freed") {
				CHECK(reinterpret_cast<std::uintptr_t>(block) % 64 == 0);
				allocator.deallocate(block, 3);
			}
		}
	}

	GIVEN("a request no allocation could satisfy") {
		JemallocAllocator<std::uint64_t> allocator;

		THEN("it throws bad_array_new_length, like std::allocator") {
			CHECK_THROWS_AS(
				allocator.allocate(std::numeric_limits<std::size_t>::max()),
				std::bad_array_new_length);
		}
	}
}

// TC-273
SCENARIO("The count returns to where it started", "[allocator][jemalloc]") {
	GIVEN("blocks of many sizes") {
		JemallocAllocator<char> allocator;
		const auto start = JemallocAllocator<char>::bytes_in_use();
		std::vector<std::pair<char *, std::size_t>> held;

		WHEN("they are taken, then freed in another order") {
			for (std::size_t size = 1; size <= (1U << 20U); size = size * 3 + 1)
				held.emplace_back(allocator.allocate(size), size);
			const auto during = JemallocAllocator<char>::bytes_in_use();

			for (std::size_t i = held.size(); i-- > 0;) {
				if (i % 2 == 0) continue;
				allocator.deallocate(held[i].first, held[i].second);
			}
			for (std::size_t i = 0; i < held.size(); i += 2)
				allocator.deallocate(held[i].first, held[i].second);

			THEN("the count counted usable size, and is exactly back") {
				std::size_t asked = 0;
				for (const auto &[block, size] : held) asked += size;
				CHECK(during >= start + asked);
				CHECK(JemallocAllocator<char>::bytes_in_use() == start);
			}
		}
	}

	GIVEN("a zero-sized request") {
		JemallocAllocator<char> allocator;
		const auto start = JemallocAllocator<char>::bytes_in_use();

		THEN("it still gets a pointer, and the count is balanced") {
			char *block = allocator.allocate(0);
			CHECK(block != nullptr);
			allocator.deallocate(block, 0);
			CHECK(JemallocAllocator<char>::bytes_in_use() == start);
		}
	}
}
