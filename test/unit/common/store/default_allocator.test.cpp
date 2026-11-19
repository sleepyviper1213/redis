#include "common/store/allocator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

namespace {
using redis::DefaultAllocator;

#ifdef REDIS_HAS_JEMALLOC
static_assert(std::is_same_v<DefaultAllocator,
							 redis::JemallocAllocator<char>>);
#else
static_assert(std::is_same_v<DefaultAllocator,
							 redis::CountingAllocator<char>>);
#endif

// What INFO and the keyspace rely on, whichever allocator was picked.
static_assert(std::is_same_v<decltype(DefaultAllocator::bytes_in_use()),
							 std::size_t>);
} // namespace

// TC-274
SCENARIO("The keyspace allocator is the one the build chose",
		 "[allocator]") {
	GIVEN("the default allocator") {
		DefaultAllocator allocator;

		WHEN("a block is taken and given back") {
			const auto before = DefaultAllocator::bytes_in_use();
			char *block       = allocator.allocate(1000);
			const auto during = DefaultAllocator::bytes_in_use();
			allocator.deallocate(block, 1000);

			THEN("the count rose by at least the block, and fell back") {
				CHECK(during >= before + 1000);
				CHECK(DefaultAllocator::bytes_in_use() == before);
			}
		}
	}
}
