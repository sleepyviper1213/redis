#pragma once
// Overflow-aware integer arithmetic over the GCC/Clang __builtin_*_overflow
// intrinsics:
// - add_sat, sub_sat, mul_sat, div_sat and saturate_cast: C++26's saturating
//   arithmetic (<numeric>, P0543), which clamps to the type's range. Where the
//   standard library already has them, these forward to it.
// - add_overflow, sub_overflow, mul_overflow: checked arithmetic, as C23's
//   ckd_add family, for callers that must report an overflow rather than clamp
//   (INCR answers "increment or decrement would overflow", as Redis does).

#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>
#include <version>

#ifdef __cpp_lib_saturation_arithmetic
#include <numeric>
#endif

namespace redis {

/// The types C++26 saturating arithmetic accepts: integers other than bool
/// and the character types.
template <typename T>
concept SaturatingInteger =
	std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool> &&
	!std::same_as<std::remove_cv_t<T>, char> &&
	!std::same_as<std::remove_cv_t<T>, wchar_t> &&
	!std::same_as<std::remove_cv_t<T>, char8_t> &&
	!std::same_as<std::remove_cv_t<T>, char16_t> &&
	!std::same_as<std::remove_cv_t<T>, char32_t>;

/// Store \p a + \p b in \p result. \return true if it overflowed; \p result
/// then holds the value wrapped to \p T.
template <SaturatingInteger T>
[[nodiscard]] constexpr bool add_overflow(T a, T b, T *result) noexcept {
	return __builtin_add_overflow(a, b, result);
}

/// Store \p a - \p b in \p result. \return true if it overflowed.
template <SaturatingInteger T>
[[nodiscard]] constexpr bool sub_overflow(T a, T b, T *result) noexcept {
	return __builtin_sub_overflow(a, b, result);
}

/// Store \p a * \p b in \p result. \return true if it overflowed.
template <SaturatingInteger T>
[[nodiscard]] constexpr bool mul_overflow(T a, T b, T *result) noexcept {
	return __builtin_mul_overflow(a, b, result);
}

/// \p a + \p b, clamped to the range of \p T.
template <SaturatingInteger T>
[[nodiscard]] constexpr T add_sat(T a, T b) noexcept {
#ifdef __cpp_lib_saturation_arithmetic
	return std::add_sat(a, b);
#else
	T result{};
	if (!add_overflow(a, b, &result)) return result;
	// Only a positive b overflows upwards; for unsigned T, b is never negative.
	return b < T{} ? std::numeric_limits<T>::min()
				   : std::numeric_limits<T>::max();
#endif
}

/// \p a - \p b, clamped to the range of \p T.
template <SaturatingInteger T>
[[nodiscard]] constexpr T sub_sat(T a, T b) noexcept {
#ifdef __cpp_lib_saturation_arithmetic
	return std::sub_sat(a, b);
#else
	T result{};
	if (!sub_overflow(a, b, &result)) return result;
	// Unsigned underflows to 0; signed overflows away from the sign of b.
	if constexpr (std::is_unsigned_v<T>) return T{};
	else
		return b < T{} ? std::numeric_limits<T>::max()
					   : std::numeric_limits<T>::min();
#endif
}

/// \p a * \p b, clamped to the range of \p T.
template <SaturatingInteger T>
[[nodiscard]] constexpr T mul_sat(T a, T b) noexcept {
#ifdef __cpp_lib_saturation_arithmetic
	return std::mul_sat(a, b);
#else
	T result{};
	if (!mul_overflow(a, b, &result)) return result;
	// The true product is negative exactly when the signs differ.
	if constexpr (std::is_unsigned_v<T>) return std::numeric_limits<T>::max();
	else
		return (a < T{}) != (b < T{}) ? std::numeric_limits<T>::min()
									  : std::numeric_limits<T>::max();
#endif
}

/// \p a / \p b, clamped to the range of \p T. \pre \p b is not 0.
template <SaturatingInteger T>
[[nodiscard]] constexpr T div_sat(T a, T b) noexcept {
#ifdef __cpp_lib_saturation_arithmetic
	return std::div_sat(a, b);
#else
	// The one quotient out of range: the most negative value divided by -1.
	if constexpr (std::is_signed_v<T>)
		if (a == std::numeric_limits<T>::min() && b == T{-1})
			return std::numeric_limits<T>::max();
	return a / b;
#endif
}

/// \p x converted to \p R, clamped to the range of \p R.
template <SaturatingInteger R, SaturatingInteger T>
[[nodiscard]] constexpr R saturate_cast(T x) noexcept {
#ifdef __cpp_lib_saturation_arithmetic
	return std::saturate_cast<R>(x);
#else
	if (std::cmp_less(x, std::numeric_limits<R>::min()))
		return std::numeric_limits<R>::min();
	if (std::cmp_greater(x, std::numeric_limits<R>::max()))
		return std::numeric_limits<R>::max();
	return static_cast<R>(x);
#endif
}

} // namespace redis
