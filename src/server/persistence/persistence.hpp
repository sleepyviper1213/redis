#pragma once
#include "server/persistence/posix.hpp"
#include "server/storage/database_fwd.hpp"

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace redis {

/**
 * \brief Snapshot persistence for one keyspace: SAVE, BGSAVE, LASTSAVE,
 * loading at startup and saving at shutdown.
 *
 * BGSAVE forks. The child writes the snapshot from its copy-on-write image
 * of the keyspace while the parent keeps serving clients; poll() collects the
 * result from the server's periodic tick.
 */
class Persistence {
public:
	using clock      = std::chrono::system_clock;
	using time_point = clock::time_point;

	explicit Persistence(std::filesystem::path file);

	Persistence(const Persistence &)            = delete;
	Persistence &operator=(const Persistence &) = delete;

	/// Waits for a running background save, so no child outlives the server.
	~Persistence();

	/// Synchronous save (SAVE). Blocks the caller until the file is durable.
	[[nodiscard]] std::expected<void, std::string> save(const Database &db);

	/// Start a background save (BGSAVE). Fails if one is already running.
	[[nodiscard]] std::expected<void, std::string>
	background_save(const Database &db);

	/// Collect a finished background save, if any. Never blocks.
	void poll();

	/// Load the snapshot file into \p db. A missing file is not an error.
	[[nodiscard]] std::expected<void, std::string> load(Database &db);

	[[nodiscard]] bool background_save_in_progress() const noexcept {
		return child_.has_value();
	}

	/// Time of the last successful save; server start if there was none.
	[[nodiscard]] time_point last_save() const noexcept { return last_save_; }

	[[nodiscard]] const std::filesystem::path &file() const noexcept {
		return file_;
	}

private:
	void finish_background_save(bool success);

	std::filesystem::path file_;
	time_point last_save_ = clock::now();
	std::optional<persistence::posix::pid_type> child_;
	time_point child_started_;
};

} // namespace redis
