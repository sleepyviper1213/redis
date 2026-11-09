#pragma once
#include <cstddef>
#include <functional>
#include <string_view>

namespace redis {

/**
 * \brief Hash for string keys that also accepts std::string_view, so lookups
 * by a view do not allocate a temporary string.
 */
struct TransparentStringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view text) const noexcept {
		return std::hash<std::string_view>{}(text);
	}
};

} // namespace redis
