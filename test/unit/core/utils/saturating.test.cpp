#include "core/utils/saturating.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using redis::add_overflow;
using redis::add_sat;
using redis::div_sat;
using redis::mul_overflow;
using redis::mul_sat;
using redis::saturate_cast;
using redis::sub_overflow;
using redis::sub_sat;

namespace {
constexpr int64_t MAX  = std::numeric_limits<int64_t>::max();
constexpr int64_t MIN  = std::numeric_limits<int64_t>::min();
constexpr uint32_t UMAX = std::numeric_limits<uint32_t>::max();
} // namespace

// Usable in constant expressions, as the standard functions are.
static_assert(add_sat<int64_t>(MAX, 1) == MAX);
static_assert(saturate_cast<uint8_t>(300) == 255);

SCENARIO("Checked arithmetic reports overflow", "[saturating]") {
	int64_t result = 0;

	THEN("in-range results are exact") {
		CHECK_FALSE(add_overflow<int64_t>(40, 2, &result));
		CHECK(result == 42);
		CHECK_FALSE(sub_overflow<int64_t>(MIN + 1, 1, &result));
		CHECK(result == MIN);
		CHECK_FALSE(mul_overflow<int64_t>(-3, 7, &result));
		CHECK(result == -21);
	}

	THEN("out-of-range results are flagged") {
		CHECK(add_overflow<int64_t>(MAX, 1, &result));
		CHECK(sub_overflow<int64_t>(MIN, 1, &result));
		CHECK(mul_overflow<int64_t>(MAX, 2, &result));
	}
}

SCENARIO("Saturating arithmetic clamps to the range", "[saturating]") {
	THEN("signed results clamp at either end") {
		CHECK(add_sat<int64_t>(MAX, 1) == MAX);
		CHECK(add_sat<int64_t>(MIN, -1) == MIN);
		CHECK(sub_sat<int64_t>(MIN, 1) == MIN);
		CHECK(sub_sat<int64_t>(MAX, -1) == MAX);
		CHECK(mul_sat<int64_t>(MAX, 2) == MAX);
		CHECK(mul_sat<int64_t>(MAX, -2) == MIN);
		CHECK(mul_sat<int64_t>(MIN, -1) == MAX);
		CHECK(div_sat<int64_t>(MIN, -1) == MAX);
	}

	THEN("unsigned results clamp at 0 and the maximum") {
		CHECK(add_sat<uint32_t>(UMAX, 1) == UMAX);
		CHECK(sub_sat<uint32_t>(0, 1) == 0);
		CHECK(mul_sat<uint32_t>(UMAX, 2) == UMAX);
	}

	THEN("in-range results are exact") {
		CHECK(add_sat<int64_t>(40, 2) == 42);
		CHECK(div_sat<int64_t>(-9, 2) == -4);
	}

	THEN("conversions clamp to the target type") {
		CHECK(saturate_cast<int8_t>(1000) == 127);
		CHECK(saturate_cast<int8_t>(-1000) == -128);
		CHECK(saturate_cast<uint16_t>(-5) == 0);
		CHECK(saturate_cast<int32_t>(int64_t{12}) == 12);
	}
}
