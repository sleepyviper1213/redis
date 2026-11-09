#pragma once
#include "core/os/child_process.hpp"
#include "server/persistence/persist_error.hpp"
#include "common/store/database_fwd.hpp"

#include <expected>
#include <filesystem>

namespace redis::persistence::detail {

/**
 * \brief The temporary file process \p pid writes the snapshot of \p file to
 * before renaming it into place. Unique per process, so a SAVE and a BGSAVE
 * child never share one. A save that is killed leaves it behind.
 */
[[nodiscard]] std::filesystem::path
snapshot_temp_path(const std::filesystem::path &file, os::pid_type pid);

/**
 * \brief Write every live key of \p db to \p file, atomically.
 *
 * The snapshot is written to a temporary file in the same directory, synced
 * to disk, and renamed over \p file; the directory is then synced so the
 * rename itself survives a crash. Readers therefore see either the old
 * snapshot or the new one, never a partial file.
 *
 * Safe to call in a forked child: it neither logs nor touches shared state.
 *
 * The format is this project's own (see snapshot.cpp), not Redis' RDB.
 */
[[nodiscard]] std::expected<void, PersistError>
save_snapshot(const Database &db, const std::filesystem::path &file);

/**
 * \brief Replace the contents of \p db with the snapshot in \p file.
 *
 * A missing file is not an error: the server starts empty. Keys whose
 * deadline passed while the server was down are skipped - unless
 * \p keep_expired, for the base of an append-only file: the commands logged
 * after it may still refer to such keys, and are followed by the DEL that
 * removes them.
 */
[[nodiscard]] std::expected<void, PersistError>
load_snapshot(Database &db, const std::filesystem::path &file,
			  bool keep_expired = false);

} // namespace redis::persistence::detail
