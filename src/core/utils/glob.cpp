#include "glob.hpp"

#include <cstddef>
#include <utility>

namespace redis {
namespace {
// Matches the bracket expression starting at pattern[p] == '[' against c.
// On return, p points just past the closing ']' (or at the end of an
// unterminated class, which then matches nothing more).
bool match_class(std::string_view pattern, size_t &p, char c) noexcept {
	++p; // skip '['
	const bool negate = p < pattern.size() && pattern[p] == '^';
	if (negate) ++p;

	bool matched = false;
	while (p < pattern.size() && pattern[p] != ']') {
		if (pattern[p] == '\\' && p + 1 < pattern.size()) {
			++p;
			if (pattern[p] == c) matched = true;
		} else if (p + 2 < pattern.size() && pattern[p + 1] == '-' &&
				   pattern[p + 2] != ']') {
			char lo = pattern[p];
			char hi = pattern[p + 2];
			if (lo > hi) std::swap(lo, hi);
			if (c >= lo && c <= hi) matched = true;
			p += 2;
		} else if (pattern[p] == c) {
			matched = true;
		}
		++p;
	}
	if (p < pattern.size()) ++p; // skip ']'
	return matched != negate;
}
} // namespace

bool glob_match(std::string_view pattern, std::string_view text) noexcept {
	size_t p = 0;
	size_t t = 0;
	// Where to resume after the most recent '*': retry with it consuming one
	// more character of text.
	size_t star_p = std::string_view::npos;
	size_t star_t = 0;

	while (t < text.size()) {
		if (p < pattern.size()) {
			switch (pattern[p]) {
			case '*':
				while (p < pattern.size() && pattern[p] == '*') ++p;
				if (p == pattern.size()) return true;
				star_p = p;
				star_t = t;
				continue;
			case '?':
				++p;
				++t;
				continue;
			case '[': {
				size_t next = p;
				if (match_class(pattern, next, text[t])) {
					p = next;
					++t;
					continue;
				}
				break;
			}
			case '\\':
				if (p + 1 < pattern.size() && pattern[p + 1] == text[t]) {
					p += 2;
					++t;
					continue;
				}
				break;
			default:
				if (pattern[p] == text[t]) {
					++p;
					++t;
					continue;
				}
				break;
			}
		}
		// Mismatch: let the last '*' absorb one more character, if any.
		if (star_p == std::string_view::npos) return false;
		p = star_p;
		t = ++star_t;
	}

	while (p < pattern.size() && pattern[p] == '*') ++p;
	return p == pattern.size();
}

} // namespace redis
