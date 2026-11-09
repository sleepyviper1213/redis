#include "core/chrono/deadline_timer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using namespace redis;

SCENARIO("DeadlineTimer saturates instead of overflowing",
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
