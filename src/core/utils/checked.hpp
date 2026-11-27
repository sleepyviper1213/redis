#pragma once
// An integer that remembers whether any step that produced it overflowed,
// after SerenityOS's AK::Checked (docs/design/patterns.md).
//
//     auto millis = Checked<int64_t>(amount);
//     millis *= MS_PER_S;
//     millis += now_ms;
//     if (millis.has_overflow()) return std::nullopt;
//     return DeadlineTimer::at_ms(millis.value());
//
// The overflow is checked once, after the computation, not after each step.
// Use it where an overflow is an error to report (INCRBY, EXPIRE); where
// clamping is the behaviour, use the saturating functions (saturating.hpp).

#include "core/error/verify.hpp"
#include "core/utils/saturating.hpp"

#include <optional>

namespace redis {

template <SaturatingInteger T>
class Checked {
public:
	constexpr Checked() noexcept = default;

	/// Implicit, as AK's: `checked += 1` and `Checked<int64_t> x = n` read as
	/// arithmetic on a T.
	// NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
	constexpr Checked(T value) noexcept : value_(value) {}

	/// Whether any step overflowed. Once true it stays true.
	[[nodiscard]] constexpr bool has_overflow() const noexcept {
		return is_overflow_;
	}

	/// The result. Only when no step overflowed: otherwise the number has
	/// wrapped and means nothing, so asking for it is a bug.
	[[nodiscard]] constexpr T value() const noexcept {
		REDIS_VERIFY(!is_overflow_, "value() of an overflowed Checked");
		return value_;
	}

	/// The result, or std::nullopt if a step overflowed.
	[[nodiscard]] constexpr std::optional<T> to_optional() const noexcept {
		if (is_overflow_) return std::nullopt;
		return value_;
	}

	constexpr Checked &operator+=(Checked other) noexcept {
		is_overflow_ = is_overflow_ || other.is_overflow_ ||
					   add_overflow(value_, other.value_, &value_);
		return *this;
	}

	constexpr Checked &operator-=(Checked other) noexcept {
		is_overflow_ = is_overflow_ || other.is_overflow_ ||
					   sub_overflow(value_, other.value_, &value_);
		return *this;
	}

	constexpr Checked &operator*=(Checked other) noexcept {
		is_overflow_ = is_overflow_ || other.is_overflow_ ||
					   mul_overflow(value_, other.value_, &value_);
		return *this;
	}

	/// 0 - x: overflows for the minimum of a signed T, and for any non-zero
	/// unsigned x.
	[[nodiscard]] constexpr Checked operator-() const noexcept {
		Checked negated;
		negated -= *this;
		return negated;
	}

	[[nodiscard]] friend constexpr Checked operator+(Checked a,
													 Checked b) noexcept {
		return a += b;
	}

	[[nodiscard]] friend constexpr Checked operator-(Checked a,
													 Checked b) noexcept {
		return a -= b;
	}

	[[nodiscard]] friend constexpr Checked operator*(Checked a,
													 Checked b) noexcept {
		return a *= b;
	}

private:
	T value_{};
	bool is_overflow_ = false;
};

} // namespace redis
