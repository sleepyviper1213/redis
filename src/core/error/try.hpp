#pragma once

// Early return for std::expected, as a GNU statement expression (GCC, Clang;
// not MSVC). The enclosing function must return a std::expected whose error
// type the failure converts to.
//
// The result names are long on purpose: a TRY nested inside another TRY's
// argument declares a second variable in an inner scope, and a short name would
// shadow the outer one (-Wshadow).

#ifndef REDIS_TRY
#define REDIS_TRY(expression)                                                  \
	({                                                                         \
		auto &&redis_try_result_ = (expression);                               \
		if (!redis_try_result_.has_value()) [[unlikely]]                       \
			return std::unexpected(                                            \
				std::forward<decltype(redis_try_result_)>(redis_try_result_)   \
					.error());                                                 \
		std::forward<decltype(redis_try_result_)>(redis_try_result_).value();  \
	})
#endif

// As REDIS_TRY, but the function's error type is E2: the failure is converted
// by E2's own converting constructor or by From<E1, E2>. With neither, it does
// not compile: falling out of the failure branch would run .value() on an error
// and throw, which is the exception the expected-based code exists to avoid.
#ifndef REDIS_TRY_FROM
#define REDIS_TRY_FROM(expr, E2)                                               \
	({                                                                         \
		auto &&redis_try_result_ = (expr);                                     \
		using redis_try_error_t_ = typename std::remove_cvref_t<               \
			decltype(redis_try_result_)>::error_type;                          \
		static_assert(std::convertible_to<redis_try_error_t_, E2> ||           \
						  FromConvertible<redis_try_error_t_, E2>,             \
					  "REDIS_TRY_FROM: the error type converts to neither "    \
					  "the function's error type nor via From<E1, E2>");       \
		if (!redis_try_result_.has_value()) [[unlikely]] {                     \
			if constexpr (std::convertible_to<redis_try_error_t_, E2>)         \
				return std::unexpected<E2>(                                    \
					E2(std::move(redis_try_result_).error()));                 \
			else                                                               \
				return std::unexpected<E2>(                                    \
					From<redis_try_error_t_, E2>::convert(                     \
						std::move(redis_try_result_).error()));                \
		}                                                                      \
		std::move(redis_try_result_).value();                                  \
	})
#endif
