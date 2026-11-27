#include "core/utils/checked.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>

using redis::Checked;

namespace {
constexpr int64_t MAX = std::numeric_limits<int64_t>::max();
constexpr int64_t MIN = std::numeric_limits<int64_t>::min();
} // namespace

// Usable in constant expressions.
static_assert((Checked<int64_t>(40) + 2).value() == 42);
static_assert((Checked<int64_t>(MAX) + 1).has_overflow());

SCENARIO("Checked arithmetic is exact while in range", "[checked]") {
	Checked<int64_t> n = 6;
	n *= 7;
	n -= 2;
	n += 2;
	CHECK_FALSE(n.has_overflow());
	CHECK(n.value() == 42);
	CHECK(n.to_optional() == std::optional<int64_t>(42));
	CHECK((-Checked<int64_t>(MAX)).value() == -MAX);
	CHECK((Checked<int64_t>(MIN + 1) - 1).value() == MIN);
}

SCENARIO("An overflow anywhere in a computation is remembered", "[checked]") {
	GIVEN("a step that overflows") {
		Checked<int64_t> n = MAX;
		n += 1;
		CHECK(n.has_overflow());
		CHECK(n.to_optional() == std::nullopt);

		THEN("it stays overflowed whatever follows") {
			n -= 1; // would bring a wrapped value back in range
			n *= 0;
			CHECK(n.has_overflow());
		}
	}

	THEN("each operation flags its own overflow") {
		CHECK((Checked<int64_t>(MIN) - 1).has_overflow());
		CHECK((Checked<int64_t>(MAX) * 2).has_overflow());
		CHECK((-Checked<int64_t>(MIN)).has_overflow());
		CHECK((-Checked<uint32_t>(1U)).has_overflow());
		CHECK_FALSE((-Checked<uint32_t>(0U)).has_overflow());
	}

	THEN("an overflowed operand overflows the result") {
		const Checked<int64_t> bad = Checked<int64_t>(MAX) + 1;
		CHECK((Checked<int64_t>(1) + bad).has_overflow());
		CHECK((bad * 0).has_overflow());
	}
}
