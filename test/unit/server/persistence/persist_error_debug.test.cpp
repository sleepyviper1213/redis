#include "server/persistence/persist_error.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <string>
#include <system_error>

using namespace redis;
using persistence::PersistError;
using persistence::PersistKind;

SCENARIO("An error has a Display form and a Debug form, as in Rust",
		 "[persist_error][debug]") {
	GIVEN("an error with several fields set") {
		const PersistError error{.kind = PersistKind::SnapshotVersion,
								 .path = "x.snap",
								 .at   = 9,
								 .code = 1};

		THEN("{} is the sentence") {
			CHECK(fmt::format("{}", error) ==
				  "x.snap has format version 9, expected 1");
		}
		THEN("{:?} is the structure on one line, set fields only") {
			CHECK(fmt::format("{:?}", error) ==
				  R"(PersistKind::SnapshotVersion { path: "x.snap", at: 9, code: 1 })");
		}
		THEN("{:#?} puts a field on each line") {
			CHECK(fmt::format("{:#?}", error) ==
				  "PersistKind::SnapshotVersion {\n"
				  "    path: \"x.snap\",\n"
				  "    at: 9,\n"
				  "    code: 1,\n"
				  "}");
		}
	}

	GIVEN("an error with no fields") {
		const PersistError error{.kind = PersistKind::SaveInProgress};

		THEN("Debug is just the kind, as Rust prints a unit variant") {
			CHECK(fmt::format("{:?}", error) == "PersistKind::SaveInProgress");
			CHECK(fmt::format("{:#?}", error) == "PersistKind::SaveInProgress");
		}
	}

	GIVEN("an error with a system reason and quoted text") {
		const auto denied = std::make_error_code(std::errc::permission_denied);
		const PersistError error{.kind   = PersistKind::ManifestBadBase,
								 .cause  = denied,
								 .quoted = "a\"b"};

		THEN("the reason shows its category, value and message") {
			CHECK(fmt::format("{:?}", error)
					  .contains(fmt::format("cause: {}:{} {:?}",
											denied.category().name(),
											denied.value(),
											denied.message())));
		}
		THEN("quoted text is escaped, so the structure stays readable") {
			CHECK(fmt::format("{:?}", error).contains(R"(quoted: "a\"b")"));
		}
	}
}

SCENARIO("Display keeps the string format specification; Debug takes none",
		 "[persist_error][debug]") {
	GIVEN("an error") {
		const PersistError error{.kind = PersistKind::NotASnapshot, .path = "a"};

		THEN("Display pads and cuts like a string") {
			CHECK(fmt::format("{:>30}", error) == "      a is not a snapshot file");
			CHECK(fmt::format("{:.10}", error) == "a is not a");
		}
		THEN("a specification after the ? or # is refused") {
			CHECK_THROWS_AS(fmt::format(fmt::runtime("{:?>5}"), error),
							fmt::format_error);
			CHECK_THROWS_AS(fmt::format(fmt::runtime("{:#}"), error),
							fmt::format_error);
			CHECK_THROWS_AS(fmt::format(fmt::runtime("{:#?x}"), error),
							fmt::format_error);
		}
	}
}
