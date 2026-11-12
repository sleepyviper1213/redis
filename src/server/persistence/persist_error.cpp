#include "persist_error.hpp"

#include "common/resp/parser.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <algorithm>
#include <array>

namespace redis::persistence {
namespace {
using enum PersistKind;

/// ": <reason>" if the system gave one, otherwise nothing.
std::string because(const PersistError &error) {
	return error.cause ? fmt::format(": {}", error.cause.message())
					   : std::string{};
}

/// The Save kinds and the steps of os::SaveFile they stand for.
struct SaveStep {
	PersistKind persist;
	os::SaveKind save;
};
constexpr std::array SAVE_STEPS{
	SaveStep{.persist=SaveCreate, .save=os::SaveKind::Create},
	SaveStep{.persist=SaveWrite, .save=os::SaveKind::Write},
	SaveStep{.persist=SaveSync, .save=os::SaveKind::Sync},
	SaveStep{.persist=SaveRename, .save=os::SaveKind::Rename},
	SaveStep{.persist=SaveSyncDirectory, .save=os::SaveKind::SyncDirectory},
};

/// A step of replacing a file, as the message names it.
std::string_view save_step(PersistKind kind) {
	const auto step = std::ranges::find(SAVE_STEPS, kind, &SaveStep::persist);
	return step == SAVE_STEPS.end() ? "save" : os::step_name(step->save);
}
} // namespace

PersistError to_persist_error(const os::SaveError &error) {
	const auto step = std::ranges::find(SAVE_STEPS, error.kind, &SaveStep::save);
	return PersistError{.kind   = step == SAVE_STEPS.end() ? SaveWrite
															: step->persist,
						.cause  = error.cause,
						.path   = error.path,
						.at     = error.at,
						.code   = error.code,
						.quoted = error.quoted};
}

std::string display(const PersistError &error) {
	const auto &path = error.path;
	switch (error.kind) {
	case CannotCreate:
		return fmt::format("cannot create {}{}", path, because(error));
	case CannotOpen:
		return fmt::format("cannot open {}{}", path, because(error));
	case CannotOpenForAppend:
		return fmt::format("cannot open {} for appending{}",
						   path,
						   because(error));
	case CannotRead:
		return fmt::format("cannot read {}{}", path, because(error));
	case CannotSize:
		return fmt::format("cannot get the size of {}{}", path, because(error));
	case CannotTruncate:
		return fmt::format("cannot truncate {}{}", path, because(error));

	case InUse:
		return fmt::format(
			"{} is in use by another server; refusing to share it",
			path);
	case CannotLock:
		return fmt::format("cannot lock {}{}", path, because(error));

	case SaveCreate:
	case SaveWrite:
	case SaveSync:
	case SaveRename:
	case SaveSyncDirectory:
		return fmt::format("cannot write {}: {} failed{}",
						   path,
						   save_step(error.kind),
						   because(error));

	case SnapshotEncode:
		return fmt::format("saving {} failed: {}",
						   path,
						   error.quoted.empty() ? "write failed"
												: error.quoted);
	case NotASnapshot: return fmt::format("{} is not a snapshot file", path);
	case SnapshotVersion:
		return fmt::format("{} has format version {}, expected {}",
						   path,
						   error.at,
						   error.code);
	case SnapshotValueType:
		return fmt::format("{} is corrupt: unknown value type {}",
						   path,
						   error.at);
	case SnapshotCorrupt:
		return fmt::format("{} is corrupt or truncated: {}",
						   path,
						   error.quoted);

	case ManifestBadBase:
		return fmt::format("{}: bad base file name '{}'", path, error.quoted);
	case ManifestBadIncr:
		return fmt::format("{}: bad incremental file name '{}'",
						   path,
						   error.quoted);
	case ManifestBadSeq:
		return fmt::format("{}: bad seq '{}'", path, error.quoted);
	case ManifestUnknownEntry:
		return fmt::format("{}: unknown entry '{}'", path, error.quoted);
	case ManifestNoIncr:
		return fmt::format("{} lists no incremental file", path);

	case LogCorrupt:
		return fmt::format("{} is corrupt at byte {}: {}",
						   path,
						   error.at,
						   static_cast<resp::Parser::RespError>(error.code));
	case LogTornInside:
		return fmt::format("{} ends in the middle of a command", path);
	case CannotWriteLog: return "cannot write the AOF";

	case SaveInProgress: return "Background save already in progress";
	case RewriteInProgress:
		return "Background append only file rewriting already in progress";
	case CannotFork: return fmt::format("Can't fork{}", because(error));
	}
	return "persistence error";
}

} // namespace redis::persistence
