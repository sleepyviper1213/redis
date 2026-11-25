#include "core/error/verify.hpp"

#include "core/error/must.hpp"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <memory>
#include <string>

// The failing side aborts the process, so it is covered where a crashing child
// is: the integration crash tests. These are the paths that must not abort.
namespace {

int evaluations = 0;

std::expected<int, std::string> forty_two() {
	++evaluations;
	return 42;
}

std::expected<std::unique_ptr<int>, int> boxed() {
	return std::make_unique<int>(7);
}

std::expected<void, int> nothing() { return {}; }

} // namespace

SCENARIO("A holding condition passes REDIS_VERIFY", "[error][verify]") {
	GIVEN("a condition that is true") {
		THEN("it is evaluated once and execution continues") {
			evaluations = 0;
			REDIS_VERIFY(forty_two().value() == 42);
			CHECK(evaluations == 1);
			REDIS_VERIFY(evaluations == 1, "with a message");
		}
	}
}

SCENARIO("REDIS_MUST and REDIS_EXPECT give the value of a success",
		 "[error][must]") {
	GIVEN("an expected that holds a value") {
		THEN("the value comes out and the expression runs once") {
			evaluations = 0;
			CHECK(REDIS_MUST(forty_two()) == 42);
			CHECK(REDIS_EXPECT(forty_two(), "known good") == 42);
			CHECK(evaluations == 2);
		}
		THEN("a move-only value is moved out") {
			const auto box = REDIS_MUST(boxed());
			REQUIRE(box != nullptr);
			CHECK(*box == 7);
		}
		THEN("an expected<void> is accepted") {
			REDIS_MUST(nothing());
			REDIS_EXPECT(nothing(), "nothing to give");
		}
	}
}
