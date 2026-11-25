#include "core/utils/glob.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using redis::is_glob_match;

SCENARIO("Matching keys against literal and wildcard patterns", "[glob]") {
	GIVEN("a literal pattern") {
		THEN("only the identical text matches") {
			CHECK(is_glob_match("", ""));
			CHECK(is_glob_match("abc", "abc"));
			CHECK_FALSE(is_glob_match("abc", "abd"));
		}
	}

	GIVEN("a pattern with '*'") {
		THEN("'*' matches any sequence, including the empty one") {
			CHECK(is_glob_match("*", ""));
			CHECK(is_glob_match("*", "anything"));
			CHECK(is_glob_match("user:*", "user:42"));
			CHECK(is_glob_match("*:42", "user:42"));
			CHECK(is_glob_match("a*b*c", "a-b-b-c"));
		}
		THEN("the literal parts must still match") {
			CHECK_FALSE(is_glob_match("user:*", "session:42"));
			CHECK_FALSE(is_glob_match("a*b*c", "a-b-b-d"));
		}
	}

	GIVEN("a pattern with '?'") {
		THEN("'?' matches exactly one character") {
			CHECK(is_glob_match("h?llo", "hello"));
			CHECK_FALSE(is_glob_match("h?llo", "hllo"));
		}
	}
}

SCENARIO("Matching keys against character classes", "[glob]") {
	GIVEN("a set of characters") {
		THEN("any listed character matches") {
			CHECK(is_glob_match("h[ae]llo", "hallo"));
			CHECK(is_glob_match("h[ae]llo", "hello"));
			CHECK_FALSE(is_glob_match("h[ae]llo", "hillo"));
		}
	}

	GIVEN("a negated set") {
		THEN("any character except the listed ones matches") {
			CHECK(is_glob_match("h[^e]llo", "hallo"));
			CHECK_FALSE(is_glob_match("h[^e]llo", "hello"));
		}
	}

	GIVEN("a range") {
		THEN("characters inside the range match, in either order") {
			CHECK(is_glob_match("key[0-9]", "key7"));
			CHECK_FALSE(is_glob_match("key[0-9]", "keyx"));
			CHECK(is_glob_match("key[9-0]", "key5"));
		}
	}
}

SCENARIO("Escaping special characters", "[glob]") {
	GIVEN("a pattern with an escaped wildcard") {
		THEN("the wildcard matches only itself") {
			CHECK(is_glob_match("a\\*b", "a*b"));
			CHECK_FALSE(is_glob_match("a\\*b", "axb"));
			CHECK(is_glob_match("what\\?", "what?"));
		}
	}
}

SCENARIO("Pathological patterns do not backtrack exponentially", "[glob]") {
	GIVEN("many stars and a long text that almost matches") {
		const std::string text(10'000, 'a');

		WHEN("matching") {
			THEN("it finishes and reports no match") {
				CHECK_FALSE(is_glob_match("*a*a*a*a*a*a*a*a*b", text));
			}
		}
	}
}
