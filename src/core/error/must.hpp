#pragma once

#include "verify.hpp"

#include <fmt/format.h>

#include <expected>
#include <source_location>
#include <string_view>
#include <utility>

// Take the value out of a std::expected that must hold one, or abort.
//
// Rust's unwrap() and expect(), for the spots where an error means a broken
// assumption, not a failure to report: a call whose input was just validated,
// a lookup of a key that was just inserted. Where the error can happen, use
// REDIS_TRY (try.hpp) and pass it on.
//
//   const auto port = REDIS_MUST(parse_port("6379"));
//   const auto fd   = REDIS_EXPECT(open_log(path), "opened before the handler");
//
// Unlike std::expected::value(), which throws bad_expected_access (an
// exception, which this code only throws to crash) or operator*, which is
// undefined behaviour on an error, a failure here reports the error itself and
// aborts through the same path as REDIS_VERIFY. If the error type has a
// fmt::formatter, its text is in the report.
//
// A statement expression, like REDIS_TRY: GCC and Clang, not MSVC. The
// expression is evaluated once, and the value is moved out of an rvalue.
namespace redis::detail {

template <typename E>
[[noreturn]] void must_failed(const E &error, std::string_view message,
							  const std::source_location &where) {
	if constexpr (fmt::is_formattable<E>::value) {
		const auto text = fmt::format("{}", error);
		verify_failed(text.c_str(), message, where);
	} else {
		verify_failed("(error type has no formatter)", message, where);
	}
}

} // namespace redis::detail

#ifndef REDIS_EXPECT
#define REDIS_EXPECT(expression, message)                                      \
	({                                                                         \
		auto &&redis_must_result_ = (expression);                              \
		if (!redis_must_result_.has_value()) [[unlikely]]                      \
			::redis::detail::must_failed(redis_must_result_.error(), (message),\
										 std::source_location::current());     \
		std::forward<decltype(redis_must_result_)>(redis_must_result_)         \
			.value();                                                          \
	})
#endif

#ifndef REDIS_MUST
#define REDIS_MUST(expression) REDIS_EXPECT(expression, std::string_view{})
#endif
