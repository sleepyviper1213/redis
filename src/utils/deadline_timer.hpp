#pragma once
#include <chrono>
#include <cstdint>

namespace redis {
/**
 * \brief The DeadlineTimer class marks a deadline in the future.
 *
 * The DeadlineTimer class is usually used to calculate future deadlines and
 * verify whether the deadline has expired. DeadlineTimer can also be used for
 * deadlines without expiration ("forever").
 *
 * The typical use-case for the class is to create a DeadlineTimer before the
 * operation in question is started, and then use seconds_left() or
 * has_expired() to determine whether to continue trying the operation.
 * DeadlineTimer objects can be passed to functions being called to execute this
 * operation so they know how long to still operate.
 */
class DeadlineTimer {
public:
	using clock_type     = std::chrono::system_clock;
	using timepoint_type = clock_type::time_point;
	using timespan_type  = clock_type::duration;

	/**
	 * \brief Constructs an expired DeadlineTimer object. For this object,
	 * seconds_left() will return 0
	 */
	DeadlineTimer();

	/**
	 * \brief Construct a DeadlineTimer object that nevr expire.
	 *
	 * For such objects, seconds_left() will return -1, deadline() will return
	 * the maximum value, and isForever() will return true
	 */
	static DeadlineTimer forever() noexcept;

	/**
	 * \brief Constructs a DeadlineTimer object with a remaining time of
	 * *remaining*.
	 *
	 * If remaining is zero or negative, this DeadlineTimer object
	 * will be mark as expired, whereas if remaining is equal to
	 * duration::max(), the object will be set to never expire.
	 *
	 * Example:
	 * \code{.cpp}
	 * using namespace std::chrono_literals;
	 * DeadlineTimer deadline(250ms);
	 * \endcode
	 */
	explicit DeadlineTimer(timespan_type remaining);

	/**
	 * Constructs a DeadlineTimer object with a deadline at deadline time point.
	 *
	 * If deadline is in the past, this DeadlineTimer object is set to expired,
	 * whereas if deadline is equal to Duration::max(), then this object is set
	 * to never expire.
	 */
	explicit DeadlineTimer(timepoint_type deadline);


	/**
	 * \brief Extend the deadline by duration of *dt*.
	 *
	 * If this deadline object was set to never expire, this function does
	 * nothing.
	 */
	void expire_after(timespan_type dt) noexcept;

	/**
	 * \brief Extend the deadline by *secs* seconds and *millisecs*
	 * milliseconds.
	 *
	 * If this deadline object was set to never expire, this function does
	 * nothing.
	 */
	void expire_after(int64_t secs = 0, int64_t millisecs = 0) noexcept;

	/**
	 * \brief Set expiration relative to a given start time.
	 *
	 * If this deadline object was set to never expire, this function does
	 * nothing.
	 */
	void expire_after(timepoint_type start, timespan_type ttl) noexcept;

	/**
	 * \brief Set expiration at an absolute timepoint.
	 * \param tp Timepoint to expire at.
	 */

	void expire_at(timepoint_type tp) noexcept;

	/**
	 * \brief Set expiration at a specific Unix time in milliseconds since
	 * epoch.
	 *
	 * If the value is in the past, this DeadlineTimer will be marked as
	 * expired.
	 */
	void expire_at_milli(int64_t millisecs_since_epoch) noexcept;

	/**
	 * \brief Set expiration at a specific Unix time in seconds since epoch.
	 *
	 * If the value is in the past, this DeadlineTimer will be marked as
	 * expired.
	 */
	void expire_at_sec(int64_t secs_since_epoch) noexcept;

	/**
	 * Returns true if this DeadlineTimer object has expired, false if there
	 * remains time left.
	 *
	 * For objects that have expired, seconds_left() will return zero and
	 * deadline() will return a time point in the past.
	 */
	[[nodiscard]] bool has_expired() const noexcept;

	/**
	 * \brief Returns true if this DeadlineTimer object never expires, false
	 * otherwise. For timers that never expire, seconds_left() always returns
	 * -1 and deadline() returns the maximum value.
	 */
	[[nodiscard]] bool is_forever() const noexcept;

	/**
	 * Returns the time remaining before the deadline.
	 */
	[[nodiscard]] timespan_type time_left() const noexcept;

	/**
	 * \brief Returns the remaining time in this DeadlineTimer object in
	 * milliseconds.
	 *
	 * If the timer has already expired, this function will return
	 * zero and it is not possible to obtain the amount of time overdue with
	 * this function. If the timer was set to never expire, this function
	 * returns -1.
	 */
	[[nodiscard]] int64_t millis_left() const noexcept;

	/**
	 * \brief Returns the remaining time in this DeadlineTimer object in
	 * seconds.
	 *
	 * If the timer has already expired, this function will return
	 * zero and it is not possible to obtain the amount of time overdue with
	 * this function. If the timer was set to never expire, this function
	 * returns -1.
	 */
	[[nodiscard]] int64_t seconds_left() const noexcept;

	/**
	 * Returns the absolute time point for the deadline store
	 */
	[[nodiscard]] const DeadlineTimer::timepoint_type &
	deadline() const noexcept;

private:
	timepoint_type expire_time_;
};

} // namespace redis
