#pragma once
#include "core/chrono/date_time.hpp"
#include "core/chrono/elapsed_timer.hpp"
#include "common/store/database_fwd.hpp"
#include "core/os/child_process.hpp"
#include "persist_error.hpp"

#include <expected>
#include <filesystem>
#include <optional>

namespace redis {

/**
 * \brief Snapshot persistence for one keyspace: SAVE, BGSAVE, LASTSAVE,
 * loading at startup and saving at shutdown.
 *
 * BGSAVE forks. The child writes the snapshot from its copy-on-write image
 * of the keyspace while the parent keeps serving clients; poll() collects the
 * result from the server's periodic tick.
 */
class SnapshotManager {
public:
	explicit SnapshotManager(std::filesystem::path file);

	SnapshotManager(const SnapshotManager &)            = delete;
	SnapshotManager &operator=(const SnapshotManager &) = delete;

	/// Waits for a running background save, so no child outlives the server.
	/// Shutdown that saves anyway calls cancel_background_save() first.
	~SnapshotManager();

	/// Synchronous save (SAVE). Blocks the caller until the file is durable.
	[[nodiscard]] std::expected<void, persistence::PersistError>
	save(const Database &db);

	/// Start a background save (BGSAVE). Fails if one is already running.
	[[nodiscard]] std::expected<void, persistence::PersistError>
	background_save(const Database &db);

	/// Collect a finished background save, if any. Never blocks.
	void poll();

	/**
	 * \brief Kill a running background save, wait for it to be gone and remove
	 * its temporary file. Shutdown calls this before the final save(), which is
	 * refused while a child exists, and whose snapshot is newer anyway.
	 */
	void cancel_background_save();

	/// Load the snapshot file into \p db. A missing file is not an error.
	[[nodiscard]] std::expected<void, persistence::PersistError>
	load(Database &db);

	[[nodiscard]] bool background_save_in_progress() const noexcept;

	/// Time of the last successful snapshot; server start if there was none.
	[[nodiscard]] DateTime last_snapshot() const noexcept;

	[[nodiscard]] const std::filesystem::path &file() const noexcept;

private:
	void finish_background_save(const os::ChildResult &result);

	std::filesystem::path file_;
	DateTime last_save_ = DateTime::current();
	DateTime child_forked_at_;
	std::optional<os::Child> child_;
	ElapsedTimer child_timer_;
};

} // namespace redis
