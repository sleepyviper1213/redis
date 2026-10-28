#include "server/storage/database.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

using namespace redis;
using namespace std::chrono_literals;

SCENARIO("Typed lookups", "[storage]") {
	GIVEN("a keyspace holding a string") {
		Database db;
		db.set("s", String("v"));

		WHEN("it is looked up as a string") {
			const auto found = db.find_as<String>("s");
			THEN("the value is returned") {
				REQUIRE(found.has_value());
				CHECK(**found == "v");
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
		(void)db.find_or_create<Set>("s");
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
	GIVEN("one short-lived, one long-lived and one persistent key") {
		Database db;
		db.set("short", String("v"));
		db.set("long", String("v"));
		db.set("forever", String("v"));
		REQUIRE(db.set_expiry("short", Database::clock::now() + 20ms));
		REQUIRE(db.set_expiry("long", Database::clock::now() + 1h));

		THEN("only keys with a deadline report one") {
			CHECK(db.expiry("short").has_value());
			CHECK_FALSE(db.expiry("forever").has_value());
		}

		AND_GIVEN("the short deadline has passed") {
			std::this_thread::sleep_for(40ms);

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
		const auto deadline = Database::clock::now() + 10ms;
		for (int i = 0; i < 10; ++i) {
			const auto key = "k" + std::to_string(i);
			db.set(key, String("v"));
			db.set_expiry(key, deadline);
		}
		std::this_thread::sleep_for(20ms);

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
			const bool existed = db.set_expiry("k", Database::clock::now() - 1s);

			THEN("the key is deleted at once") {
				CHECK(existed);
				CHECK_FALSE(db.contains("k"));
			}
		}

		WHEN("a deadline is set on a missing key") {
			THEN("nothing happens") {
				CHECK_FALSE(
					db.set_expiry("missing", Database::clock::now() + 1s));
			}
		}
	}
}

SCENARIO("Keeping the expiry indexes consistent", "[storage][expiry]") {
	GIVEN("a key whose deadline is replaced, persisted, then erased") {
		Database db;
		db.set("k", String("v"));
		db.set_expiry("k", Database::clock::now() + 10ms);
		db.set_expiry("k", Database::clock::now() + 1h);
		REQUIRE(db.persist("k"));
		REQUIRE_FALSE(db.persist("k"));
		db.set_expiry("k", Database::clock::now() + 10ms);
		REQUIRE(db.erase("k"));

		WHEN("the key is recreated without a deadline and old ones pass") {
			db.set("k", String("again"));
			std::this_thread::sleep_for(20ms);
			const auto removed = db.active_expire_cycle(100);

			THEN("no stale deadline expires the new key") {
				CHECK(removed == 0);
				CHECK(db.contains("k"));
			}
		}
	}
}
