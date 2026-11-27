// Fuzzes is_glob_match, the matcher behind KEYS (TC-329).
//
// The input is a pattern and a text, split at the first line feed (no line
// feed: the whole input is the pattern, the text is empty). The matcher must
// agree with a reference that tokenises the pattern and matches by dynamic
// programming. The class grammar ([...], ranges, escapes) is restated from
// glob.hpp; what is independent is the matching, which is where the single-star
// backtracking of glob.cpp could go wrong. A pattern escaping every byte of the
// text must match it. Slow inputs (exponential backtracking) show as
// libFuzzer timeouts.

#include "core/error/verify.hpp"
#include "core/utils/glob.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/// One unit of a pattern, as glob.hpp defines them.
struct Token {
	enum class Kind : uint8_t {
		Star,  ///< `*`
		Any,   ///< `?`
		Byte,  ///< a literal, escaped or not
		Class, ///< `[...]`
		Never, ///< a trailing `\`, which glob.cpp matches with nothing
	};

	Kind kind;
	char byte       = 0;     ///< Byte
	bool is_negated = false; ///< Class
	/// Class: inclusive ranges; a single byte is a range of one.
	std::vector<std::pair<char, char>> ranges{};

	[[nodiscard]] bool is_in_class(char c) const {
		const bool is_listed =
			std::ranges::any_of(ranges, [c](const std::pair<char, char> &r) {
				return r.first <= c && c <= r.second;
			});
		return is_listed != is_negated;
	}

	/// Whether this token, not being a star, consumes \p c.
	[[nodiscard]] bool matches(char c) const {
		switch (kind) {
		case Kind::Any: return true;
		case Kind::Byte: return byte == c;
		case Kind::Class: return is_in_class(c);
		case Kind::Star:
		case Kind::Never: return false;
		}
		return false;
	}
};

/// The bracket expression at pattern[p] == '['; leaves \p p past its `]`, or
/// at the end of an unterminated one.
Token parse_class(std::string_view pattern, size_t &p) {
	Token token{.kind = Token::Kind::Class};
	++p;
	if (p < pattern.size() && pattern[p] == '^') {
		token.is_negated = true;
		++p;
	}
	while (p < pattern.size() && pattern[p] != ']') {
		if (pattern[p] == '\\' && p + 1 < pattern.size()) {
			token.ranges.emplace_back(pattern[p + 1], pattern[p + 1]);
			p += 2;
		} else if (p + 2 < pattern.size() && pattern[p + 1] == '-' &&
				   pattern[p + 2] != ']') {
			token.ranges.push_back(std::minmax(pattern[p], pattern[p + 2]));
			p += 3;
		} else {
			token.ranges.emplace_back(pattern[p], pattern[p]);
			++p;
		}
	}
	if (p < pattern.size()) ++p;
	return token;
}

std::vector<Token> tokenise(std::string_view pattern) {
	std::vector<Token> tokens;
	size_t p = 0;
	while (p < pattern.size()) {
		switch (pattern[p]) {
		case '*':
			tokens.push_back({.kind = Token::Kind::Star});
			++p;
			break;
		case '?':
			tokens.push_back({.kind = Token::Kind::Any});
			++p;
			break;
		case '[': tokens.push_back(parse_class(pattern, p)); break;
		case '\\':
			if (p + 1 < pattern.size()) {
				tokens.push_back({.kind = Token::Kind::Byte, .byte = pattern[p + 1]});
				p += 2;
			} else {
				tokens.push_back({.kind = Token::Kind::Never});
				++p;
			}
			break;
		default:
			tokens.push_back({.kind = Token::Kind::Byte, .byte = pattern[p]});
			++p;
			break;
		}
	}
	return tokens;
}

/// Whether \p tokens match all of \p text: row i holds, for every j, whether
/// tokens[i..] match text[j..]. Built from the last token back, two rows kept.
bool reference_match(const std::vector<Token> &tokens, std::string_view text) {
	const size_t n = text.size();
	std::vector<char> next(n + 1, 0); // tokens[i + 1..]
	std::vector<char> row(n + 1, 0);  // tokens[i..]
	next[n] = 1;                      // no tokens match only the empty rest
	for (size_t i = tokens.size(); i-- > 0;) {
		const Token &token = tokens[i];
		for (size_t j = n + 1; j-- > 0;) {
			if (token.kind == Token::Kind::Star)
				row[j] = static_cast<char>(next[j] || (j < n && row[j + 1]));
			else
				row[j] = static_cast<char>(j < n && token.matches(text[j]) &&
										   next[j + 1]);
		}
		std::swap(row, next);
	}
	return next[0] != 0;
}

/// A pattern that matches \p text and nothing else.
std::string escape(std::string_view text) {
	std::string pattern;
	pattern.reserve(text.size() * 2);
	for (const char c : text) {
		pattern += '\\';
		pattern += c;
	}
	return pattern;
}

/// The reference costs O(|pattern| * |text|); beyond this it is skipped.
/// scripts/fuzz.sh caps the input at 4 KiB, which always fits.
constexpr size_t MAX_REFERENCE_CELLS = size_t{1} << 23;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	const std::string_view input(reinterpret_cast<const char *>(data), size);
	const size_t split           = input.find('\n');
	const std::string_view pattern = input.substr(0, split);
	const std::string_view text =
		split == std::string_view::npos ? std::string_view{}
										: input.substr(split + 1);

	const bool is_matched = redis::is_glob_match(pattern, text);
	if ((pattern.size() + 1) * (text.size() + 1) <= MAX_REFERENCE_CELLS)
		REDIS_VERIFY(is_matched == reference_match(tokenise(pattern), text),
					 "is_glob_match disagrees with the reference matcher");
	REDIS_VERIFY(redis::is_glob_match(escape(text), text),
				 "a text does not match itself escaped");
	return 0;
}
