#include "core/chrono/date_time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>

using namespace redis;
using namespace std::chrono_literals;

SCENARIO("DateTime is Unix milliseconds with saturating arithmetic",
		 "[core][date_time]") {
	constexpr auto big = std::numeric_limits<int64_t>::max();

	THEN("it round-trips Unix timestamps") {
		CHECK(DateTime::from_msecs_since_epoch(1500).to_msecs_since_epoch() ==
			  1500);
		CHECK(DateTime::from_secs_since_epoch(100).to_msecs_since_epoch() ==
			  100'000);
		CHECK(DateTime::from_msecs_since_epoch(1999).to_secs_since_epoch() == 1);
		CHECK(DateTime::from_msecs_since_epoch(-1).to_secs_since_epoch() == -1);
	}
	THEN("seconds that overflow saturate") {
		CHECK(DateTime::from_secs_since_epoch(big) == DateTime::distant_future());
		CHECK(DateTime::from_secs_since_epoch(-big) == DateTime::distant_past());
	}
	THEN("adding and measuring saturate instead of wrapping") {
		CHECK(DateTime::distant_future().add_secs(1) ==
			  DateTime::distant_future());
		CHECK(DateTime::distant_past().add_msecs(-1) ==
			  DateTime::distant_past());
		CHECK(DateTime::distant_future().add_secs(big) ==
			  DateTime::distant_future());
		CHECK(DateTime::from_msecs_since_epoch(10).add_msecs(5) ==
			  DateTime::from_msecs_since_epoch(15));
		CHECK(DateTime::from_msecs_since_epoch(10).msecs_to(
				  DateTime::from_msecs_since_epoch(25)) == 15);
		CHECK(DateTime::distant_future().msecs_to(DateTime::distant_past()) ==
			  std::numeric_limits<int64_t>::min());
	}
	THEN("instants order chronologically") {
		CHECK(DateTime::from_msecs_since_epoch(1) <
			  DateTime::from_msecs_since_epoch(2));
		CHECK(DateTime::distant_past() < DateTime::current());
		CHECK(DateTime::current() < DateTime::distant_future());
	}
	THEN("it converts to and from std::chrono time points") {
		const auto tp = DateTime::from_msecs_since_epoch(1234).to_std_sys_milliseconds();
		CHECK(tp.time_since_epoch() == 1234ms);
		CHECK(DateTime::from_std_time_point(tp) ==
			  DateTime::from_msecs_since_epoch(1234));
		// A finer time point rounds down, also before the epoch.
		using Ns = std::chrono::sys_time<std::chrono::nanoseconds>;
		CHECK(DateTime::from_std_time_point(Ns(1'999'999ns)) ==
			  DateTime::from_msecs_since_epoch(1));
		CHECK(DateTime::from_std_time_point(Ns(-1ns)) ==
			  DateTime::from_msecs_since_epoch(-1));
		// A coarser time point scales exactly and saturates instead of
		// overflowing.
		using Sec = std::chrono::sys_seconds;
		CHECK(DateTime::from_std_time_point(Sec(2s)) ==
			  DateTime::from_msecs_since_epoch(2000));
		CHECK(DateTime::from_std_time_point(
				  Sec(std::chrono::seconds(big))) ==
			  DateTime::distant_future());
		CHECK(DateTime::from_std_time_point(
				  Sec(std::chrono::seconds(std::numeric_limits<int64_t>::min()))) ==
			  DateTime::distant_past());
	}
}
