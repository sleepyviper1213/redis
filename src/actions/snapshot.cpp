#include "snapshot.hpp"

#include "db.hpp"
#include "error.hpp"
#include "utils/create_temp_file.hpp"

#include <cereal/archives/binary.hpp>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <sys/errno.h>
#include <sys/types.h>

#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#error "Windows is not supported yet"
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/wait.h>

#include <unistd.h>
#endif
namespace redis {

namespace fs = std::filesystem;

Snapshot::Snapshot(std::vector<Command> list) : commands_(std::move(list)) {}

void Snapshot::clear() { commands_.clear(); }

void Snapshot::addCommand(Command command) {
	if (!command.isModifiableCommand()) return;
	std::unique_lock<std::shared_mutex> _(cmds_mtx_);
	if (command.isFlushDatabase()) {
		spdlog::info("[Snapshot] Flush log");
		clear();
	} else {
		commands_.push_back(command);
		spdlog::info("[Snapshot] Executed: {}", command);
	}
}

// void quick_exit_handler() { fmt::println("quick_exit handler"); }
#if defined(_WIN32)
#error "Windows is not supported yet"
#elif defined(__APPLE__) || defined(__linux__)

ErrorOr<void> Snapshot::createFrom(Db &db) {
	const auto tmp_file = createTempFile();

	const pid_t rc = fork();
	if (rc == -1) return failed("[Snapshot] fork failed", std::errc{errno});

	if (rc == 0) {
		spdlog::debug("[Snapshot] Writing snapshot at child process {}",
					  getpid());
		try {
			std::ofstream file(tmp_file, std::ios::binary);
			cereal::BinaryOutputArchive oarchive(file);

			// Snapshot all TTL at this current time point
			std::vector<std::reference_wrapper<Command>> expires;

			for (auto &cmd : commands_) {
				auto ttl = db.cmdTTL(cmd.args());
				if (ttl.has_value()) expires.emplace_back(cmd);
				else if (ttl.error().containsErrorMessage("TTL")) continue;
				spdlog::debug("writing: {}", cmd);
				oarchive(cmd);
			}
			for (auto &cmd : expires) oarchive(cmd.get());
		} catch (const std::ofstream::failure &e) {
			// Handle open error
			spdlog::error("[Snapshot] Error {} with file: {} ",
						  e.what(),
						  tmp_file);
			std::exit(EXIT_FAILURE);
		}
#if __APPLE__
		// TODO: undefined behavior in a multithreaded process.
		// destroys some static variables which are actively being used by
		// another thread, causing that thread to access freed memory and
		// destroyed objects, registers an atexit() handler to clean up
		// global resources,
		std::exit(EXIT_SUCCESS);
#elif __linux__
		// quick_exit does not call C++ static destructors
		std::quick_exit(EXIT_SUCCESS);
#endif
	} else {
		spdlog::debug("[Snapshot] Parent {} waits for child", getpid());
		int status = 0;
		waitpid(rc, &status, 0);
		if (!WIFEXITED(status)) {
			spdlog::error(
				"Child failed with status {}. Killing all running children.\n",
				WEXITSTATUS(status));
			// TODO: exit immediately or return
			std::exit(EXIT_FAILURE);
			// return failed("process failure", std::errc::no_such_process);
		}

		std::unique_lock<std::shared_mutex> lck{file_mtx_};
		const std::filesystem::path SNAPSHOT_FILE = "redis.snap";
		const fs::path backup_file = SNAPSHOT_FILE.stem().concat(".snap.bak");

		try {
			spdlog::debug("[Snapshot] Atomic rename to commit snapshot");
			if (fs::exists(SNAPSHOT_FILE))
				fs::rename(SNAPSHOT_FILE, backup_file);
			fs::rename(tmp_file, SNAPSHOT_FILE);

			if (fs::exists(backup_file)) fs::remove(backup_file);
		} catch (const fs::filesystem_error &e) {
			spdlog::error("{}", e.what());
			return failed("[SNAPSHOT] Failed to commit snapshot",
						  std::errc::io_error);
		}
		return {};
	}
}

#endif

// ErrorOr<Db> Snapshot::restoreSnapshot() {
// std::unique_lock<std::shared_mutex> lck_file(file_mtx);
//
//
// std::list<Command> cmds;
// Db db;
// while (true) {
// Command x = TRY(load<Command>(PATH));
// cmds.push_back(x);
// TODO : Handle error here TRY(db->execute(x));
//}
//
// std::unique_lock<std::shared_mutex> lck_cmd(cmds_mtx);
// this->clear();
// commands = std::move(cmds);
// return ErrorOr<Db>{db};
//}
} // namespace redis
