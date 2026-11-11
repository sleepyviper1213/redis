#pragma once

#include "core/utils/saturating.hpp"

#include <chrono>
#include <cstdint>
#include <type_traits>

namespace redis {

/**
 * \brief A point on the wall clock: milliseconds since the Unix epoch.
 *
 * Not a monotonic clock: use ElapsedTimer to measure intervals.
 *
 * Unlike std::chrono::time_point it never converts implicitly to a finer unit,
 * which is what made a maximal millisecond time point overflow as nanoseconds.
 *
 * Undefined behaviour: none of the members has any for any argument; every
 * arithmetic operation saturates. The only ways to reach it are through the
 * standard library, after the value leaves this class: see
 * to_std_sys_milliseconds(). from_std_time_point() rejects at compile time
 * the one input type that would be unsafe.
 */
class DateTime {
public:
	/// The Unix epoch.
	DateTime() noexcept = default;

	/// The current wall-clock time
	[[nodiscard]] static DateTime current() noexcept;

	/// Construction from Unix timestamps; seconds saturate on overflow.
	[[nodiscard]] static DateTime from_msecs_since_epoch(int64_t ms) noexcept;
	[[nodiscard]] static DateTime from_secs_since_epoch(int64_t secs) noexcept;

	/**
	 * \brief From a standard system-clock time point of any integral
	 * resolution, rounded down to a whole millisecond. Values beyond the range
	 * saturate to distant_past() / distant_future().
	 *
	 * \warning Undefined behaviour is avoided by construction, not by
	 * checking: \p Duration must have an integral rep. Converting a
	 * floating-point count that is NaN, infinite or out of the int64 range
	 * would be undefined behaviour, so such a time point does not compile.
	 */
	template <typename Duration>
	[[nodiscard]] static DateTime
	from_std_time_point(std::chrono::sys_time<Duration> tp) noexcept {
		static_assert(std::is_integral_v<typename Duration::rep>,
					  "a floating-point time point cannot be converted safely");
		using Period = Duration::period;
		if constexpr (std::ratio_greater_equal_v<Period, std::milli>) {
			// A whole number of milliseconds per tick: scale with saturation,
			// since the standard's own conversion would overflow near the
			// limits.
			using Factor = std::ratio_divide<Period, std::milli>;
			static_assert(Factor::den == 1);
			return from_msecs_since_epoch(
				mul_sat(static_cast<int64_t>(tp.time_since_epoch().count()),
						static_cast<int64_t>(Factor::num)));
		} else {
			return from_msecs_since_epoch(
				std::chrono::floor<std::chrono::milliseconds>(tp)
					.time_since_epoch()
					.count());
		}
	}

	/// The instants before and after every other one. A DateTime is never
	/// "null" or invalid: arithmetic that would leave the range lands here.
	[[nodiscard]] static DateTime distant_past() noexcept;
	[[nodiscard]] static DateTime distant_future() noexcept;

	[[nodiscard]] int64_t to_msecs_since_epoch() const noexcept;

	/// Whole seconds, rounded down
	[[nodiscard]] int64_t to_secs_since_epoch() const noexcept;

	/**
	 * \brief This instant as a standard system-clock time point, for
	 * interoperating with the standard library. The conversion itself is exact
	 * and never overflows.
	 *
	 * \warning Undefined behaviour, in the standard library and not here: near
	 * the extremes (distant_past() / distant_future(), and anything within
	 * about 292 years of them when counted in nanoseconds) the result must not
	 * be converted to a finer unit such as system_clock::time_point
	 * (nanoseconds), nor formatted as a calendar date. Those signed
	 * conversions overflow. Compare with distant_past() / distant_future()
	 * first, or stay in milliseconds.
	 */
	[[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds>
	to_std_sys_milliseconds() const noexcept;

	/// This instant moved by \p ms / \p secs, saturating.
	[[nodiscard]] DateTime add_msecs(int64_t ms) const noexcept;
	[[nodiscard]] DateTime add_secs(int64_t secs) const noexcept;

	/// Milliseconds from this instant to \p other, negative if \p other is
	/// earlier; saturating.
	[[nodiscard]] int64_t msecs_to(DateTime other) const noexcept;

	friend std::strong_ordering operator<=>(DateTime,
											DateTime) noexcept = default;

private:
	explicit DateTime(int64_t ms) noexcept : ms_(ms) {}

	int64_t ms_ = 0;
};

} // namespace redis
