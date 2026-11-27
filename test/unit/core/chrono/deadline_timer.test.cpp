#include "core/chrono/deadline_timer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <thread>

using namespace redis;
using namespace std::chrono_literals;

namespace {
/// Whether \p Timer offers the Unix-time members (only WallTime should).
template <class Timer>
concept HasUnixTime = requires(Timer t) {
	Timer::at_ms(int64_t{0});
	t.millis_since_epoch();
};
} // namespace

// The clocks are what the concepts say (ADR-0013).
static_assert(Clock<WallTime> && !MonotonicClock<WallTime>);
static_assert(MonotonicClock<TickClock>);
static_assert(MonotonicClock<std::chrono::steady_clock>);
static_assert(HasUnixTime<DeadlineTimer>);
static_assert(!HasUnixTime<BasicDeadlineTimer<std::chrono::steady_clock>>);
static_assert(!HasUnixTime<TickDeadlineTimer>);
static_assert(std::same_as<DeadlineTimer::time_point,
						   std::chrono::sys_time<std::chrono::milliseconds>>);

SCENARIO("TC-123: DeadlineTimer saturates instead of overflowing",
		 "[core][deadline_timer]") {
	constexpr auto big = std::numeric_limits<int64_t>::max();
	constexpr auto low = std::numeric_limits<int64_t>::min();

	THEN("a huge relative deadline is forever") {
		CHECK(DeadlineTimer::after_s(big).is_forever());
		CHECK(DeadlineTimer::after_ms(big).is_forever());
		CHECK_FALSE(DeadlineTimer::after_s(big).has_expired());
	}
	THEN("a huge negative deadline is long past, not wrapped") {
		CHECK(DeadlineTimer::after_s(low).has_expired());
		CHECK(DeadlineTimer::at_ms(low).has_expired());
		CHECK(DeadlineTimer::at_s(low).remaining_time() ==
			  DeadlineTimer::duration::zero());
	}
	THEN("a huge absolute deadline is forever") {
		CHECK(DeadlineTimer::at_s(big).is_forever());
		CHECK(DeadlineTimer::at_ms(big) == DeadlineTimer::forever());
	}
	THEN("ordinary deadlines are exact") {
		CHECK(DeadlineTimer::at_s(100).millis_since_epoch() == 100'000);
		CHECK(DeadlineTimer::at_ms(1500).millis_since_epoch() == 1500);
		CHECK_FALSE(DeadlineTimer::after_s(60).is_forever());
	}
}

SCENARIO("TC-326: a deadline saturates on any clock",
		 "[core][deadline_timer]") {
	using Clock = std::chrono::steady_clock;
	constexpr auto huge = std::chrono::hours::max();

	THEN("the largest duration from now is forever, on every clock") {
		CHECK(BasicDeadlineTimer<Clock>::after(huge).is_forever());
		CHECK(TickDeadlineTimer::after(huge).is_forever());
		CHECK(DeadlineTimer::after(huge).is_forever());
	}

	THEN("the most negative duration is long past, not wrapped") {
		const auto past = BasicDeadlineTimer<Clock>::after(std::chrono::hours::min());
		CHECK(past.has_expired());
		CHECK(past.remaining_time() == Clock::duration::zero());
		CHECK(DeadlineTimer::after(std::chrono::hours::min()).has_expired());
	}

	THEN("forever never expires, and has all the time the clock has left") {
		const auto forever = BasicDeadlineTimer<Clock>::forever();
		CHECK(forever.is_forever());
		CHECK_FALSE(forever.has_expired());
		// max() less now(): exactly max() only on a clock at its epoch. Read
		// now() first, so the timer's own reading is the later one.
		const auto at_most = Clock::duration::max() -
							 Clock::now().time_since_epoch();
		CHECK(forever.remaining_time() <= at_most);
		CHECK(forever.remaining_time() > at_most - 1h);
	}
}

SCENARIO("TC-327: DeadlineTimer keeps Unix times on the wall clock",
		 "[core][deadline_timer]") {
	constexpr int64_t at_ms = 1'700'000'000'050;
	const auto timer = DeadlineTimer::at_ms(at_ms);

	THEN("its Unix time and its DateTime are the same instant") {
		CHECK(timer.millis_since_epoch() == at_ms);
		CHECK(timer.date_time() == DateTime::from_msecs_since_epoch(at_ms));
		CHECK(DeadlineTimer::at(timer.date_time()) == timer);
		CHECK(DeadlineTimer::at_s(100).date_time() ==
			  DateTime::from_secs_since_epoch(100));
		CHECK(DeadlineTimer::forever().date_time() == DateTime::distant_future());
	}
}

// Waits on the real clock: excluded under the sanitizers (DX-93).
SCENARIO("TC-333: a wall-clock deadline expires on the real clock",
		 "[core][deadline_timer][timing]") {
	const auto timer = DeadlineTimer::after_ms(50);
	CHECK(timer.remaining_time() <= 50ms);
	CHECK_FALSE(timer.has_expired());

	std::this_thread::sleep_for(300ms);
	CHECK(timer.has_expired());
	CHECK(timer.remaining_time() == DeadlineTimer::duration::zero());
}
