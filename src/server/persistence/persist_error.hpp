#pragma once

#include "core/error/failure_format.hpp"
#include "core/os/save_error.hpp"

#include <cstdint>
#include <string>

namespace redis::persistence {

/**
 * \brief What can go wrong keeping the data on disk. The fields each kind
 * fills in (see Failure) are in brackets; the wording is in format_as().
 */
enum class PersistKind : std::uint8_t {
	// A file or directory could not be used. [path, cause]
	CannotCreate,
	CannotOpen,
	CannotOpenForAppend,
	CannotRead,
	CannotSize,
	CannotTruncate,

	// The lock that keeps a second server out of the directory.
	InUse,      ///< [path = the directory]
	CannotLock, ///< [path = the lock file, cause]

	// Replacing a file as a whole (SaveFile): the step that failed.
	// [path = the file being replaced, cause (empty when only "failed")]
	SaveCreate,
	SaveWrite,
	SaveSync,
	SaveRename,
	SaveSyncDirectory,

	// A snapshot.
	SnapshotEncode,  ///< [path, quoted = why; empty for a plain write failure]
	NotASnapshot,    ///< [path]
	SnapshotVersion, ///< [path, at = the version found, code = expected]
	SnapshotValueType, ///< [path, at = the unknown type's number]
	SnapshotCorrupt,   ///< [path, quoted = what the reader said]

	// The manifest. [path = the manifest, quoted = the offending text]
	ManifestBadBase,
	ManifestBadIncr,
	ManifestBadSeq,
	ManifestUnknownEntry,
	ManifestNoIncr, ///< [path] only

	// The log.
	LogCorrupt,    ///< [path, at = byte offset, code = the resp parse error]
	LogTornInside, ///< [path]: ends in the middle of a command, not at the end
	CannotWriteLog,

	// Background work.
	SaveInProgress,
	RewriteInProgress,
	CannotFork, ///< [cause]
};

using PersistError = Failure<PersistKind>;

/// The sentence for \p error, without any protocol prefix: the log line, the
/// startup message, and (behind "ERR ") the reply to a client all use it.
[[nodiscard]] std::string display(const PersistError &error);

/// A failed SaveFile step as a PersistError (kind SaveCreate .. SaveSyncDirectory),
/// with the same path and cause.
[[nodiscard]] PersistError to_persist_error(const os::SaveError &error);

} // namespace redis::persistence
