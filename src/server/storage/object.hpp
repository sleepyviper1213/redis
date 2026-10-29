#pragma once

#if defined(REDIS_KEYSPACE_MAP_ABSL)
#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#elif !defined(REDIS_KEYSPACE_MAP_STD)
#include <boost/unordered/unordered_flat_map.hpp>
#endif

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace redis {

/**
 * \brief Hash for std::string keys that also accepts std::string_view, so
 * lookups by a view do not allocate a temporary std::string.
 */
struct TransparentStringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view s) const noexcept {
		return std::hash<std::string_view>{}(s);
	}
};

template <typename V>
using StringMap = std::unordered_map<std::string, V, TransparentStringHash,
									 std::equal_to<>>;

/**
 * \brief Keyspace map, selected at configure time with
 * -DREDIS_KEYSPACE_MAP=boost|absl|std (default boost).
 *
 * boost and absl are open-addressing (Swiss-table style): one flat array, SIMD
 * group probing, no per-entry allocation. References and iterators are
 * invalidated by any insertion, so callers must not hold an Object* across a
 * later insert into the same Database. std keeps node stability and serves as
 * the baseline. Set and Hash values always use the node-based containers
 * because SPOP relies on the bucket interface.
 */
#if defined(REDIS_KEYSPACE_MAP_ABSL)
struct AbslStringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view s) const noexcept {
		return absl::Hash<std::string_view>{}(s);
	}
};
template <typename V>
using KeyMap =
	absl::flat_hash_map<std::string, V, AbslStringHash, std::equal_to<>>;
#elif defined(REDIS_KEYSPACE_MAP_STD)
template <typename V>
using KeyMap = StringMap<V>;
#else
template <typename V>
using KeyMap = boost::unordered_flat_map<std::string, V, TransparentStringHash,
										 std::equal_to<>>;
#endif

using StringSet =
	std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>;

/// The value types a key can hold. Each command family works on one of them.
using String = std::string;
using List   = std::deque<std::string>;
using Set    = StringSet;
using Hash   = StringMap<std::string>;

using Object = std::variant<String, List, Set, Hash>;

/**
 * \brief The name the TYPE command reports for \p object.
 */
[[nodiscard]] constexpr std::string_view type_name(const Object &object) {
	constexpr std::string_view names[] = {"string", "list", "set", "hash"};
	return names[object.index()];
}

} // namespace redis
