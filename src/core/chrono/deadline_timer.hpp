#pragma once

#include "clock.hpp"
#include "core/utils/saturating.hpp"
#include "date_time.hpp"
#include "tick_clock.hpp"
#include "wall_time.hpp"

#include <chrono>
#include <concepts>
#include <cstdint>
#include <ratio>
#include <type_traits>

namespace redis {

namespace detail {
/**
 * \brief \p d in the duration \p To, clamped to the range of \p To instead of
 * overflowing. Integral counts only: a floating-point count could be NaN.
 */
template <class To, class Rep, class Period>
[[nodiscard]] constexpr To
saturating_duration_cast(std::chrono::duration<Rep, Period> d) noexcept {
	static_assert(std::is_integral_v<Rep> &&
					  std::is_integral_v<typename To::rep>,
				  "a floating-point duration cannot be converted safely");
	using ToRep = typename To::rep;
	using Ratio = std::ratio_divide<Period, typename To::period>;
	if constexpr (Ratio::den == 1) {
		// To is as fine or finer: scale up, which is where overflow lives.
		return To(mul_sat(saturate_cast<ToRep>(d.count()),
						  static_cast<ToRep>(Ratio::num)));
	} else {
		static_assert(Ratio::num == 1, "periods must divide one another");
		// To is coarser: dividing cannot overflow.
		return std::chrono::floor<To>(d);
	}
}
} // namespace detail

/**
 * \brief A deadline on the clock \p C (ADR-0013): when something must be done
 * by, as BasicElapsedTimer is how long something has run. API modelled after
 * QDeadlineTimer, with the clock a template parameter as in BasicElapsedTimer.
 *
 * Every construction saturates: a deadline too far ahead is forever(), one too
 * far behind is the earliest instant, never a wrapped value.
 *
 * \tparam C Any std::chrono clock. steady_clock by default; WallTime for
 * deadlines that are Unix times (DeadlineTimer); TickClock on the hot path.
 */
template <Clock C = std::chrono::steady_clock>
class BasicDeadlineTimer {
public:
	using clock      = C;
	using duration   = typename C::duration;
	using time_point = typename C::time_point;

	/// A deadline at \p deadline.
	[[nodiscard]] static BasicDeadlineTimer at(time_point deadline) noexcept {
		return BasicDeadlineTimer(deadline);
	}

	/// A deadline \p from_now after now; saturating. Negative is in the past.
	template <class Rep, class Period>
	[[nodiscard]] static BasicDeadlineTimer
	after(std::chrono::duration<Rep, Period> from_now) noexcept {
		const auto now = C::now().time_since_epoch().count();
		const auto by =
			detail::saturating_duration_cast<duration>(from_now).count();
		return BasicDeadlineTimer(time_point(duration(add_sat(now, by))));
	}

	/// A deadline that never passes.
	[[nodiscard]] static BasicDeadlineTimer forever() noexcept {
		return BasicDeadlineTimer(time_point::max());
	}

	[[nodiscard]] time_point deadline() const noexcept { return deadline_; }

	[[nodiscard]] bool is_forever() const noexcept {
		return deadline_ == time_point::max();
	}

	[[nodiscard]] bool has_expired() const noexcept {
		return C::now() >= deadline_;
	}

	/// Time left, clamped to zero once the deadline has passed.
	[[nodiscard]] duration remaining_time() const noexcept {
		const auto left = sub_sat(deadline_.time_since_epoch().count(),
								  C::now().time_since_epoch().count());
		return left > 0 ? duration(left) : duration::zero();
	}

	// Unix times, which mean something only on the wall clock (EXPIRE,
	// EXPIREAT, PXAT, snapshot and append-only-file deadlines).

	/// A deadline at \p deadline.
	[[nodiscard]] static BasicDeadlineTimer at(DateTime deadline) noexcept
		requires std::same_as<C, WallTime>
	{
		return at(deadline.to_std_sys_milliseconds());
	}

	/// A deadline at an absolute Unix time (EXPIREAT, PEXPIREAT); saturating.
	[[nodiscard]] static BasicDeadlineTimer at_s(int64_t unix_secs) noexcept
		requires std::same_as<C, WallTime>
	{
		return at(DateTime::from_secs_since_epoch(unix_secs));
	}

	[[nodiscard]] static BasicDeadlineTimer at_ms(int64_t unix_ms) noexcept
		requires std::same_as<C, WallTime>
	{
		return at(DateTime::from_msecs_since_epoch(unix_ms));
	}

	/// A deadline \p secs / \p millis after now (EXPIRE, PEXPIRE); saturating.
	[[nodiscard]] static BasicDeadlineTimer after_s(int64_t secs) noexcept
		requires std::same_as<C, WallTime>
	{
		return after(std::chrono::seconds(secs));
	}

	[[nodiscard]] static BasicDeadlineTimer after_ms(int64_t millis) noexcept
		requires std::same_as<C, WallTime>
	{
		return after(std::chrono::milliseconds(millis));
	}

	/// The deadline as milliseconds since the Unix epoch, for the wire and
	/// the files, which exchange POSIX times.
	[[nodiscard]] int64_t millis_since_epoch() const noexcept
		requires std::same_as<C, WallTime>
	{
		return deadline_.time_since_epoch().count();
	}

	/// The deadline as a DateTime: exact, both being Unix milliseconds.
	[[nodiscard]] DateTime date_time() const noexcept
		requires std::same_as<C, WallTime>
	{
		return DateTime::from_msecs_since_epoch(millis_since_epoch());
	}

	friend auto operator<=>(const BasicDeadlineTimer &,
							const BasicDeadlineTimer &) = default;

private:
	explicit BasicDeadlineTimer(time_point deadline) noexcept
		: deadline_(deadline) {}

	time_point deadline_;
};

/// Key expiry: deadlines that are Unix times, on the wall clock (DX-91).
using DeadlineTimer = BasicDeadlineTimer<WallTime>;

/// Timeouts checked on the hot path, where steady_clock is costly on macOS.
using TickDeadlineTimer = BasicDeadlineTimer<TickClock>;

} // namespace redis
