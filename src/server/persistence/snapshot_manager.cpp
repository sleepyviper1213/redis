#include "snapshot_manager.hpp"

#include "core/logging.hpp"
#include "server/persistence/child_process.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep (db.size())
#include "server/storage/database_fwd.hpp"
#include "snapshot.hpp"

#include <fmt/chrono.h> // IWYU pragma: keep (fmt formatters used by the logger)
#include <fmt/format.h> // IWYU pragma: keep
#include <fmt/std.h>    // IWYU pragma: keep
#include <spdlog/logger.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::persistence);
}
} // namespace

SnapshotManager::SnapshotManager(std::filesystem::path file) : file_(std::move(file)) {}

// Only logging can throw here; ending the process on that is acceptable.
SnapshotManager::~SnapshotManager() { // NOLINT(bugprone-exception-escape)
	// Wait, don't kill: when the server ends without a final save (an I/O
	// thread failed), a save in flight is the best snapshot there will be.
	if (child_)
		if (const auto done = persistence::posix::reap(*child_, true))
			finish_background_save(*done);
}

std::expected<void, std::string> SnapshotManager::save(const Database &db) {
	if (child_)
		return std::unexpected("ERR Background save already in progress");

	const auto result = persistence::save_snapshot(db, file_);
	if (!result) {
		log().error("{}", result.error());
		return std::unexpected("ERR " + result.error());
	}
	last_save_ = clock::now();
	log().info("DB saved on disk to {}", file_);
	return {};
}

std::expected<void, std::string>
SnapshotManager::background_save(const Database &db) {
	if (child_)
		return std::unexpected("ERR Background save already in progress");

	auto child = persistence::posix::fork_and_run(
		[&] { return persistence::save_snapshot(db, file_); });
	if (!child) {
		log().error("Can't fork for background save: {}",
					child.error().message());
		return std::unexpected("ERR Can't fork: " + child.error().message());
	}

	log().info("Background saving started by pid {}", child->pid());
	child_.emplace(std::move(*child));
	child_started_ = clock::now();
	return {};
}

void SnapshotManager::cancel_background_save() {
	if (!child_) return;
	const auto pid = child_->pid();
	log().warn("Killing the background saving child {}", pid);
	persistence::posix::kill_child(pid);
	// Wait until it is gone: a kill is not instant, and the child must not
	// recreate its temporary file after it has been removed.
	(void)persistence::posix::reap(*child_, true);
	std::error_code ignored;
	std::filesystem::remove(persistence::snapshot_temp_path(file_, pid),
							ignored);
	child_.reset();
}

void SnapshotManager::poll() {
	if (!child_) return;
	if (const auto done = persistence::posix::reap(*child_, false))
		finish_background_save(*done);
}

std::expected<void, std::string> SnapshotManager::load(Database &db) {
	auto result = persistence::load_snapshot(db, file_);
	if (result) log().info("DB loaded from {}: {} keys", file_, db.size());
	return result;
}

void SnapshotManager::finish_background_save(
	const persistence::posix::ChildResult &result) {
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		clock::now() - child_started_);
	if (result.success) {
		last_save_ = child_started_; // the snapshot reflects fork time
		log().info("Background saving terminated with success in {}", elapsed);
	} else {
		log().error("Background saving failed after {}: {}", elapsed,
					result.error);
		// A child that died mid-save (killed, out of memory) could not remove
		// its temporary file itself.
		std::error_code ignored;
		std::filesystem::remove(
			persistence::snapshot_temp_path(file_, child_->pid()), ignored);
	}
	child_.reset();
}

} // namespace redis
