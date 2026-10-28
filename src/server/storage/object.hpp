#pragma once

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
