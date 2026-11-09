#pragma once

#if defined(REDIS_KEYSPACE_MAP_ABSL)
#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#elif !defined(REDIS_KEYSPACE_MAP_STD)
#include <boost/unordered/unordered_flat_map.hpp>
#endif

#include "basic_simple_dynamic_string.hpp"
#include "core/utils/ring_buffer.hpp"
#include "core/utils/transparent_hash.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace redis::detail {

#if defined(REDIS_KEYSPACE_MAP_ABSL)
struct AbslStringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view text) const noexcept {
		return absl::Hash<std::string_view>{}(text);
	}
};
#endif

/**
 * \brief The strings and containers a keyspace stores, for allocator \p
 * Allocator.
 *
 * \p Allocator is any type meeting the standard Allocator requirements; its
 * value type does not matter, since every element and node type is rebound from
 * it.
 */
template <typename Allocator>
struct BasicValueTypes {
	template <typename T>
	using rebound =
		typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

	using String =
		std::basic_string<char, std::char_traits<char>, rebound<char>>;
	using List = RingBuffer<String, rebound<String>>;
	using Set  = std::unordered_set<String, TransparentStringHash,
									std::equal_to<>, rebound<String>>;

	template <typename V>
	using StringMap =
		std::unordered_map<String, V, TransparentStringHash, std::equal_to<>,
						   rebound<std::pair<const String, V>>>;

	using Hash = StringMap<String>;

	/// Keys of the keyspace: immutable, one pointer, as Redis's sds.
	using Key = BasicSimpleDynamicString<char, rebound<char>>;

	/**
	 * \brief Keyspace map, selected at configure time with
	 * -DREDIS_KEYSPACE_MAP=absl|boost|std (default absl).
	 *
	 * boost and absl are open-addressing (Swiss-table style): one flat array,
	 * SIMD group probing, no per-entry allocation. References and iterators
	 * are invalidated by any insertion, so callers must not hold an Object* (or
	 * a String*) across a later insert into the same Database. std keeps node
	 * stability and serves as the baseline. Set and Hash values always use the
	 * node-based containers because SPOP relies on the bucket interface.
	 */
#if defined(REDIS_KEYSPACE_MAP_ABSL)
	template <typename V>
	using KeyMap = absl::flat_hash_map<Key, V, AbslStringHash, std::equal_to<>,
									   rebound<std::pair<const Key, V>>>;
#elif defined(REDIS_KEYSPACE_MAP_STD)
	template <typename V>
	using KeyMap =
		std::unordered_map<Key, V, TransparentStringHash, std::equal_to<>,
						   rebound<std::pair<const Key, V>>>;
#else
	template <typename V>
	using KeyMap = boost::unordered_flat_map<Key, V, TransparentStringHash,
											 std::equal_to<>,
											 rebound<std::pair<const Key, V>>>;
#endif
};

} // namespace redis::detail
