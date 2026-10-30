#pragma once
#include "server/storage/database_fwd.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace redis::persistence {

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
[[nodiscard]] std::expected<void, std::string>
save_snapshot(const Database &db, const std::filesystem::path &file);

/**
 * \brief Replace the contents of \p db with the snapshot in \p file.
 *
 * A missing file is not an error: the server starts empty. Keys whose
 * deadline passed while the server was down are skipped.
 */
[[nodiscard]] std::expected<void, std::string>
load_snapshot(Database &db, const std::filesystem::path &file);

} // namespace redis::persistence
