#pragma once

#include <string_view>

namespace redis {

/**
 * \brief Match \p text against a Redis-style glob \p pattern.
 *
 * Supports the same syntax as Redis' KEYS command:
 * - `*` matches any sequence, including the empty one
 * - `?` matches exactly one character
 * - `[abc]`, `[a-z]` and `[^a]` match one character from (or not from) a set
 * - `\x` matches `x` literally
 *
 * Runs in O(|pattern| * |text|) worst case: `*` backtracks only to the most
 * recent star, never recursively.
 */
[[nodiscard]] bool glob_match(std::string_view pattern,
							  std::string_view text) noexcept;

} // namespace redis
