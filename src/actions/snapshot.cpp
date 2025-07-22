
#include "snapshot.hpp"

#include "command.hpp"
#include "db.hpp"

#include <cereal/archives/binary.hpp>
#include <fmt/chrono.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <mutex>
#if defined(_WIN32)
#error "Windows is not supported yet"
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif
void Snapshot::clear() { commands.clear(); }

void Snapshot::addCommand(Command command) {
	if (command.isModifiableCommand()) return;
	std::unique_lock<std::shared_mutex> _(cmds_mtx);
	if (command.isFlushDatabase()) {
		spdlog::info("[Snapshot] Flush log");
		clear();
	} else {
		spdlog::info("[Snapshot] Executing command: {}", command);
		commands.push_back(command);
	}
}

std::filesystem::path createTempFile() {
	return fmt::format("{}{}",
					   std::chrono::system_clock::now(),
					   PATH.extension());
}
#if defined(_WIN32)
#error "Windows is not supported yet"
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h>

ErrorOr<void> Snapshot::createFrom(Db &db) {
	namespace fs            = std::filesystem;
	const auto tmp_filename = createTempFile();

	spdlog::debug(
		"[Snapshot] Isolate the snapshot-writing logic to child process");
	auto rc = fork();
	if (rc == -1) return failed("fork failed", std::errc{errno});

	fs::path filename = PATH;
	spdlog::debug("[Snapshot] Child process writes snapshot");
	if (rc == 0) {
		std::ofstream of(tmp_filename, std::ios::binary);
		if (!of.is_open())
			return failed("No snapshot file created",
						  std::errc::no_such_file_or_directory);
		cereal::BinaryOutputArchive oarchive(of);

		// Snapshot all TTL at this current time point
		std::vector<Command> expires;

		for (auto &cmd : commands) {
			auto ttl = db.cmdTTL(cmd.args());
			if (ttl.has_value()) expires.push_back(cmd);
			else if (ttl.error().containsErrorMessage("TTL")) continue;
			oarchive(cmd);
		}
		for (auto &cmd : expires) oarchive(cmd);
		of.close();
		return {};
	}

	spdlog::debug("[Snapshot] Parent waits for child");
	int status = 0;
	waitpid(rc, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return failed("Something is wrong", std::errc::io_error);

	std::unique_lock<std::shared_mutex> lck{file_mtx};
	fs::perms perms = fs::status(filename).permissions();
	bool can_read   = (perms & fs::perms::owner_read) != fs::perms::none;
	if (can_read) {
		filename.replace_extension(".bak");
		return failed("Unable to rename temporary snapshot",
					  std::errc::permission_denied);
	}

	spdlog::debug("[Snapshot] Atomic rename to commit snapshot");
	fs::rename(tmp_filename, filename);
	fs::remove(filename / ".bak");
	return {};
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
