#include "server/persistence/persist_error.hpp"

#include "common/resp/parser.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <cstdint>
#include <string>
#include <system_error>

using namespace redis;
using persistence::PersistError;
using persistence::PersistKind;

namespace {
const std::error_code DENIED =
	std::make_error_code(std::errc::permission_denied);

std::string text(const PersistError &error) {
	return fmt::format("{}", error);
}
} // namespace

SCENARIO("A persistence failure is worded in one place", "[persist_error]") {
	GIVEN("a failure to use a file, with the system's reason") {
		const auto error = [](PersistKind kind) {
			return PersistError{.kind = kind, .cause = DENIED, .path = "x.snap"};
		};

		THEN("each kind names what was being done, the file, and the reason") {
			CHECK(text(error(PersistKind::CannotCreate))
					  .starts_with("cannot create x.snap: "));
			CHECK(text(error(PersistKind::CannotOpen))
					  .starts_with("cannot open x.snap: "));
			CHECK(text(error(PersistKind::CannotOpenForAppend))
					  .starts_with("cannot open x.snap for appending: "));
			CHECK(text(error(PersistKind::CannotRead))
					  .starts_with("cannot read x.snap: "));
			CHECK(text(error(PersistKind::CannotSize))
					  .starts_with("cannot get the size of x.snap: "));
			CHECK(text(error(PersistKind::CannotTruncate))
					  .starts_with("cannot truncate x.snap: "));
			CHECK(text(error(PersistKind::CannotLock))
					  .starts_with("cannot lock x.snap: "));
			CHECK(text(error(PersistKind::CannotOpen)).ends_with(DENIED.message()));
		}
	}

	GIVEN("a failure without a system reason") {
		THEN("there is no dangling colon") {
			CHECK(text({.kind = PersistKind::CannotOpen, .path = "x.snap"}) ==
				  "cannot open x.snap");
		}
	}

	GIVEN("the steps of replacing a file") {
		const auto step = [](PersistKind kind) {
			return text({.kind = kind, .cause = DENIED, .path = "x.snap"});
		};

		THEN("each says which step failed, for which file") {
			CHECK(step(PersistKind::SaveCreate)
					  .starts_with("cannot write x.snap: create failed: "));
			CHECK(step(PersistKind::SaveWrite)
					  .starts_with("cannot write x.snap: write failed: "));
			CHECK(step(PersistKind::SaveSync)
					  .starts_with("cannot write x.snap: fsync failed: "));
			CHECK(step(PersistKind::SaveRename)
					  .starts_with("cannot write x.snap: rename failed: "));
			CHECK(step(PersistKind::SaveSyncDirectory)
					  .starts_with(
						  "cannot write x.snap: fsync of the directory failed: "));
		}
	}

	GIVEN("the ways a snapshot can be wrong") {
		THEN("each says what is wrong with it") {
			CHECK(text({.kind = PersistKind::NotASnapshot, .path = "x.snap"}) ==
				  "x.snap is not a snapshot file");
			CHECK(text({.kind = PersistKind::SnapshotVersion,
						.path = "x.snap",
						.at   = 9,
						.code = 1}) ==
				  "x.snap has format version 9, expected 1");
			CHECK(text({.kind = PersistKind::SnapshotValueType,
						.path = "x.snap",
						.at   = 7}) == "x.snap is corrupt: unknown value type 7");
			CHECK(text({.kind   = PersistKind::SnapshotCorrupt,
						.path   = "x.snap",
						.quoted = "eof"}) == "x.snap is corrupt or truncated: eof");
			CHECK(text({.kind = PersistKind::SnapshotEncode, .path = "x.snap"}) ==
				  "saving x.snap failed: write failed");
			CHECK(text({.kind   = PersistKind::SnapshotEncode,
						.path   = "x.snap",
						.quoted = "boom"}) == "saving x.snap failed: boom");
		}
	}

	GIVEN("the ways a manifest can be wrong") {
		const auto manifest = [](PersistKind kind) {
			return text({.kind = kind, .path = "m", .quoted = "../evil"});
		};

		THEN("the offending text is quoted") {
			CHECK(manifest(PersistKind::ManifestBadBase) ==
				  "m: bad base file name '../evil'");
			CHECK(manifest(PersistKind::ManifestBadIncr) ==
				  "m: bad incremental file name '../evil'");
			CHECK(manifest(PersistKind::ManifestBadSeq) ==
				  "m: bad seq '../evil'");
			CHECK(manifest(PersistKind::ManifestUnknownEntry) ==
				  "m: unknown entry '../evil'");
			CHECK(text({.kind = PersistKind::ManifestNoIncr, .path = "m"}) ==
				  "m lists no incremental file");
		}
	}

	GIVEN("the ways the log can be wrong") {
		THEN("a corrupt log gives the byte and the parse error") {
			const auto corrupt = text(
				{.kind = PersistKind::LogCorrupt,
				 .path = "l",
				 .at   = 12,
				 .code = static_cast<std::int64_t>(
					 resp::Parser::RespError::PARSE_NUMBER)});
			CHECK(corrupt.starts_with("l is corrupt at byte 12: "));
		}
		THEN("a log torn in the middle says so") {
			CHECK(text({.kind = PersistKind::LogTornInside, .path = "l"}) ==
				  "l ends in the middle of a command");
		}
	}

	GIVEN("the directory lock and background work") {
		THEN("each has its own sentence") {
			CHECK(text({.kind = PersistKind::InUse, .path = "d"}) ==
				  "d is in use by another server; refusing to share it");
			CHECK(text({.kind = PersistKind::SaveInProgress}) ==
				  "Background save already in progress");
			CHECK(text({.kind = PersistKind::RewriteInProgress}) ==
				  "Background append only file rewriting already in progress");
			CHECK(text({.kind = PersistKind::CannotWriteLog}) ==
				  "cannot write the AOF");
			CHECK(text({.kind = PersistKind::CannotFork, .cause = DENIED}) ==
				  "Can't fork: " + DENIED.message());
		}
	}
}

SCENARIO("An error takes the format specification of a string",
		 "[persist_error]") {
	GIVEN("a short error") {
		// "a is not a snapshot file" is 24 characters.
		const PersistError error{.kind = PersistKind::NotASnapshot, .path = "a"};

		THEN("width, fill and alignment apply") {
			CHECK(fmt::format("{:>30}", error) == "      a is not a snapshot file");
			CHECK(fmt::format("{:<28}|", error) == "a is not a snapshot file    |");
			CHECK(fmt::format("{:*^28}", error) == "**a is not a snapshot file**");
		}
		THEN("precision cuts it") {
			CHECK(fmt::format("{:.10}", error) == "a is not a");
		}
	}
}

SCENARIO("Equal errors compare equal", "[persist_error]") {
	GIVEN("two errors built the same way") {
		const PersistError one{.kind  = PersistKind::CannotOpen,
							   .cause = DENIED,
							   .path  = "p",
							   .at    = 3};
		const PersistError two{.kind  = PersistKind::CannotOpen,
							   .cause = DENIED,
							   .path  = "p",
							   .at    = 3};

		THEN("they are equal, and differ when any field does") {
			CHECK(one == two);
			CHECK_FALSE(one == PersistError{.kind  = PersistKind::CannotOpen,
											.cause = DENIED,
											.path  = "q",
											.at    = 3});
		}
	}
}
