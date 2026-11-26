#include "core/chrono/deadline_timer.hpp"

#include "core/chrono/elapsed_timer.hpp"
#include "support/simulated_clock.hpp"
#include "support/simulated_wall_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>

using namespace redis;
using namespace std::chrono_literals;

namespace {
/// Whether \p Timer offers the Unix-time members (only WallTime should).
template <class Timer>
concept HasUnixTime = requires(Timer t) {
	Timer::at_ms(int64_t{0});
	t.millis_since_epoch();
};

struct Tc325;
struct Tc326;
struct TagA;
struct TagB;
} // namespace

// TC-328: the clocks are what the concepts say (ADR-0013).
static_assert(Clock<WallTime> && !MonotonicClock<WallTime>);
static_assert(MonotonicClock<TickClock>);
static_assert(MonotonicClock<std::chrono::steady_clock>);
static_assert(MonotonicClock<test::SimulatedClock<TagA>>);
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

SCENARIO("TC-325: both timers read a simulated clock exactly",
		 "[core][deadline_timer][simulated_clock]") {
	using Clock = test::SimulatedClock<Tc325>;
	Clock::reset();
	BasicElapsedTimer<Clock> elapsed;
	const auto deadline = BasicDeadlineTimer<Clock>::after(100ms);

	Clock::advance(40ms);
	CHECK(elapsed.elapsed() == 40ms);
	CHECK(deadline.remaining_time() == 60ms);
	CHECK_FALSE(deadline.has_expired());

	Clock::advance(60ms);
	CHECK(elapsed.elapsed() == 100ms);
	CHECK(deadline.remaining_time() == Clock::duration::zero());
	CHECK(deadline.has_expired());

	CHECK(elapsed.restart() == 100ms);
	CHECK(elapsed.elapsed() == 0ms);
}

SCENARIO("TC-326: a deadline saturates on any clock",
		 "[core][deadline_timer][simulated_clock]") {
	using Clock = test::SimulatedClock<Tc326>;
	Clock::reset();
	constexpr auto huge = std::chrono::hours::max();

	THEN("the largest duration from now is forever, on every clock") {
		CHECK(BasicDeadlineTimer<Clock>::after(huge).is_forever());
		CHECK(BasicDeadlineTimer<std::chrono::steady_clock>::after(huge)
				  .is_forever());
		CHECK(TickDeadlineTimer::after(huge).is_forever());
		CHECK(DeadlineTimer::after(huge).is_forever());
	}

	THEN("the most negative duration is long past, not wrapped") {
		const auto past = BasicDeadlineTimer<Clock>::after(std::chrono::hours::min());
		CHECK(past.has_expired());
		CHECK(past.remaining_time() == Clock::duration::zero());
		CHECK(DeadlineTimer::after(std::chrono::hours::min()).has_expired());
	}

	THEN("forever never expires, and has all the time there is") {
		const auto forever = BasicDeadlineTimer<Clock>::forever();
		CHECK(forever.remaining_time() == Clock::duration::max());
		Clock::advance(24h);
		CHECK_FALSE(forever.has_expired());
	}
}

SCENARIO("TC-327: DeadlineTimer keeps Unix times on the wall clock",
		 "[core][deadline_timer][simulated_clock]") {
	constexpr int64_t start_ms = 1'700'000'000'000;
	test::SimulatedWallClock clock(DateTime::from_msecs_since_epoch(start_ms));
	const auto timer = DeadlineTimer::after_ms(50);

	THEN("its Unix time and its DateTime are the same instant") {
		CHECK(timer.millis_since_epoch() == start_ms + 50);
		CHECK(timer.date_time() == DateTime::from_msecs_since_epoch(start_ms + 50));
		CHECK(DeadlineTimer::at(timer.date_time()) == timer);
		CHECK(DeadlineTimer::at_s(100).date_time() ==
			  DateTime::from_secs_since_epoch(100));
		CHECK(DeadlineTimer::forever().date_time() == DateTime::distant_future());
	}

	THEN("it expires when the simulated wall clock reaches it") {
		CHECK(timer.remaining_time() == 50ms);
		clock.advance(49ms);
		CHECK_FALSE(timer.has_expired());
		clock.advance(1ms);
		CHECK(timer.has_expired());
	}
}

SCENARIO("TC-328: a simulated clock moves only when told",
		 "[core][simulated_clock]") {
	using A = test::SimulatedClock<TagA>;
	using B = test::SimulatedClock<TagB>;
	A::reset();
	B::reset();

	A::advance(5ms);
	THEN("it moved by exactly that much, and another tag did not move") {
		CHECK(A::now().time_since_epoch() == 5ms);
		CHECK(B::now().time_since_epoch() == B::duration::zero());
	}
	THEN("reset() brings it back to the origin") {
		A::reset();
		CHECK(A::now().time_since_epoch() == A::duration::zero());
	}
}
