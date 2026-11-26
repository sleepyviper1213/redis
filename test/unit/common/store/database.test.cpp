#include "common/store/database.hpp"
#include "support/simulated_wall_clock.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <chrono>
#include <string>

using namespace redis;
using namespace std::chrono_literals;

SCENARIO("Typed lookups", "[storage]") {
	GIVEN("a keyspace holding a string") {
		Database db;
		db.set("s", String("v"));

		WHEN("it is looked up as a string") {
			const auto found = db.find_string("s");
			THEN("the value is returned") {
				REQUIRE(found.has_value());
				CHECK((*found)->string() == "v");
			}
		}

		WHEN("it is looked up as a list") {
			THEN("the lookup reports a wrong type") {
				CHECK_FALSE(db.find_as<List>("s").has_value());
			}
		}

		WHEN("a missing key is looked up") {
			const auto missing = db.find_as<List>("missing");
			THEN("the lookup succeeds with no value") {
				REQUIRE(missing.has_value());
				CHECK(*missing == nullptr);
			}
		}
	}
}

SCENARIO("Creating collections on first write", "[storage]") {
	GIVEN("an empty keyspace") {
		Database db;

		WHEN("a list is created and written, then requested again") {
			auto list = db.find_or_create<List>("l");
			REQUIRE(list.has_value());
			(*list)->push_back("a");
			const auto again = db.find_or_create<List>("l");

			THEN("the same list is returned, not a new one") {
				REQUIRE(again.has_value());
				CHECK((*again)->size() == 1);
			}
		}
	}
}

SCENARIO("Removing empty collections", "[storage]") {
	GIVEN("an empty set and an empty string") {
		Database db;
		REQUIRE(db.find_or_create<Set>("s"));
		db.set("str", String(""));

		WHEN("both are checked for emptiness") {
			db.erase_if_empty("s");
			db.erase_if_empty("str");

			THEN("the set is removed but the string is kept") {
				CHECK_FALSE(db.contains("s"));
				CHECK(db.contains("str"));
			}
		}
	}
}

SCENARIO("Expiring keys", "[storage][expiry]") {
	// Deadlines pass when the clock is moved, never early (DX-90).
	test::SimulatedWallClock clock;

	GIVEN("one short-lived, one long-lived and one persistent key") {
		Database db;
		db.set("short", String("v"));
		db.set("long", String("v"));
		db.set("forever", String("v"));
		REQUIRE(db.set_expiry("short", DeadlineTimer::after(20ms)) ==
			Database::ExpiryResult::Set);
		REQUIRE(db.set_expiry("long", DeadlineTimer::after(1h)) ==
			Database::ExpiryResult::Set);

		THEN("only keys with a deadline report one") {
			CHECK(db.expiry("short").has_value());
			CHECK_FALSE(db.expiry("forever").has_value());
		}

		AND_GIVEN("the short deadline has passed") {
			clock.advance(20ms);

			WHEN("the active expiry cycle runs") {
				REQUIRE(db.size() == 3);
				const auto removed = db.active_expire_cycle(100);

				THEN("only the due key is removed") {
					CHECK(removed == 1);
					CHECK(db.size() == 2);
					CHECK(db.contains("long"));
				}
			}

			WHEN("the expired key is read") {
				const Object *value = db.find("short");

				THEN("it is gone, removed by the lookup itself") {
					CHECK(value == nullptr);
					CHECK(db.size() == 2);
				}
			}
		}
	}

	GIVEN("ten keys that expired at the same time") {
		Database db;
		// The simulated clock stands still while the keys are added, so none
		// expires early and the counts below are exact.
		const DeadlineTimer deadline = DeadlineTimer::after(200ms);
		constexpr int KEYS_WITH_DEADLINE = 10;
		for (int i = 0; i < KEYS_WITH_DEADLINE; ++i) {
			const auto key = fmt::format("k{}", i);
			db.set(key, String("v"));
			(void)db.set_expiry(key, deadline);
		}
		clock.advance(200ms);

		WHEN("cycles run with a budget of 4, then a large one") {
			const auto first  = db.active_expire_cycle(4);
			const auto second = db.active_expire_cycle(100);

			THEN("each cycle stays within its budget and all keys go") {
				CHECK(first == 4);
				CHECK(second == 6);
				CHECK(db.size() == 0);
			}
		}
	}

	GIVEN("a key") {
		Database db;
		db.set("k", String("v"));

		WHEN("its deadline is set in the past") {
			const auto result = db.set_expiry("k", DeadlineTimer::after(-1s));

			THEN("the key is deleted at once") {
				CHECK(result == Database::ExpiryResult::Deleted);
				CHECK_FALSE(db.contains("k"));
			}
		}

		WHEN("a deadline is set on a missing key") {
			THEN("nothing happens") {
				CHECK(db.set_expiry("missing", DeadlineTimer::after(1s)) ==
					  Database::ExpiryResult::Refused);
			}
		}
	}
}

SCENARIO("Keeping the expiry indexes consistent", "[storage][expiry]") {
	GIVEN("a key whose deadline is replaced, persisted, then erased") {
		test::SimulatedWallClock clock;
		Database db;
		db.set("k", String("v"));
		(void)db.set_expiry("k", DeadlineTimer::after(10ms));
		(void)db.set_expiry("k", DeadlineTimer::after(1h));
		REQUIRE(db.persist("k"));
		REQUIRE_FALSE(db.persist("k"));
		(void)db.set_expiry("k", DeadlineTimer::after(10ms));
		REQUIRE(db.erase("k"));

		WHEN("the key is recreated without a deadline and old ones pass") {
			db.set("k", String("again"));
			clock.advance(10ms);
			const auto removed = db.active_expire_cycle(100);

			THEN("no stale deadline expires the new key") {
				CHECK(removed == 0);
				CHECK(db.contains("k"));
			}
		}
	}
}

// Deadlines that have passed are made without waiting: with
// expiry off a deadline in the past is stored, not acted on, so the order and
// the count do not depend on the clock.
SCENARIO("The active expiry cycle is bounded and takes the oldest first",
		 "[storage][expiry]") {
	GIVEN("2500 keys whose deadlines have passed") {
		Database db;
		db.set_expiry_enabled(false);
		constexpr int EXPIRED_KEYS = 2500;
		for (int i = 0; i < EXPIRED_KEYS; ++i) {
			const auto key = fmt::format("k{}", i);
			db.set(key, String("v"));
			(void)db.set_expiry(key, DeadlineTimer::after(-1s));
		}
		db.set_expiry_enabled(true);

		WHEN("TC-108: cycles run with a budget of 1000") {
			THEN("each removes at most 1000, until none is left") {
				CHECK(db.active_expire_cycle(1000) == 1000);
				CHECK(db.active_expire_cycle(1000) == 1000);
				CHECK(db.active_expire_cycle(1000) == 500);
				CHECK(db.active_expire_cycle(1000) == 0);
				CHECK(db.size() == 0);
				CHECK(db.expired_keys() == EXPIRED_KEYS);
			}
		}
	}

	GIVEN("three due keys, added newest first, then oldest, then middle") {
		Database db;
		db.set_expiry_enabled(false);
		for (const auto &[key, age] : {std::pair{"newest", 1s},
									   std::pair{"oldest", 3s},
									   std::pair{"middle", 2s}}) {
			db.set(key, String("v"));
			(void)db.set_expiry(key, DeadlineTimer::after(-age));
		}
		db.set_expiry_enabled(true);

		WHEN("TC-109: a cycle with a budget of 2 runs") {
			REQUIRE(db.active_expire_cycle(2) == 2);

			THEN("the two oldest deadlines went, whatever the order added") {
				db.set_expiry_enabled(false); // look without removing
				CHECK_FALSE(db.contains("oldest"));
				CHECK_FALSE(db.contains("middle"));
				CHECK(db.contains("newest"));
			}
		}
	}

	GIVEN("a due key, a key due in an hour and a key without a deadline") {
		Database db;
		db.set_expiry_enabled(false);
		db.set("due", String("v"));
		(void)db.set_expiry("due", DeadlineTimer::after(-1s));
		db.set_expiry_enabled(true);
		db.set("later", String("v"));
		(void)db.set_expiry("later", DeadlineTimer::after(1h));
		db.set("forever", String("v"));

		WHEN("TC-110: a cycle runs") {
			const auto removed = db.active_expire_cycle(100);

			THEN("only the due key goes; the others keep their deadlines") {
				CHECK(removed == 1);
				CHECK(db.size() == 2);
				CHECK(db.expiry("later").has_value());
				CHECK_FALSE(db.expiry("forever").has_value());
				CHECK(db.expiring() == 1);
			}
		}
	}
}
