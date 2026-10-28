#include "persistence.hpp"

#include "core/logging.hpp"
#include "server/storage/database.hpp"
#include "snapshot.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/std.h>

#include <utility>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::persistence);
}
} // namespace

Persistence::Persistence(std::filesystem::path file) : file_(std::move(file)) {}

Persistence::~Persistence() {
	if (child_)
		finish_background_save(*persistence::posix::reap(*child_, true));
}

std::expected<void, std::string> Persistence::save(const Database &db) {
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
Persistence::background_save(const Database &db) {
	if (child_)
		return std::unexpected("ERR Background save already in progress");

	auto child = persistence::posix::fork_and_run(
		[&] { return persistence::save_snapshot(db, file_).has_value(); });
	if (!child) {
		log().error("Can't fork for background save: {}",
					child.error().message());
		return std::unexpected("ERR Can't fork: " + child.error().message());
	}

	child_         = *child;
	child_started_ = clock::now();
	log().info("Background saving started by pid {}", *child_);
	return {};
}

void Persistence::poll() {
	if (!child_) return;
	if (const auto done = persistence::posix::reap(*child_, false))
		finish_background_save(*done);
}

std::expected<void, std::string> Persistence::load(Database &db) {
	auto result = persistence::load_snapshot(db, file_);
	if (result) log().info("DB loaded from {}: {} keys", file_, db.size());
	return result;
}

void Persistence::finish_background_save(bool success) {
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		clock::now() - child_started_);
	if (success) {
		last_save_ = child_started_; // the snapshot reflects fork time
		log().info("Background saving terminated with success in {}", elapsed);
	} else {
		log().error("Background saving failed after {}", elapsed);
	}
	child_.reset();
}

} // namespace redis
