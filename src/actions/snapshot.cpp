
#include "snapshot.hpp"

#include "db.hpp"
#include "utils.hpp"

#include <cereal/archives/binary.hpp>
#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <memory>
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
	std::filesystem::path filename(
		fmt::format("{}", std::chrono::system_clock::now()));
	return filename / PATH.extension();
}
#if defined(_WIN32)
#error "Windows is not supported yet"
#elif defined(__APPLE__) || defined(__linux__)
#include <unistd.h>

ErrorOr<void> Snapshot::createSnapshot(Db &db) {
	namespace fs            = std::filesystem;
	const auto tmp_filename = createTempFile();

	auto rc = fork();
	if (rc < 0) return false;

	fs::path filename = PATH;
	if (rc == 0) {
		std::ofstream of(tmp_filename,
						 std::ios::out | std::ios::binary | std::ios::trunc);
		if (!of.is_open())
			return failed("No snapshot file created",
						  std::errc::no_such_file_or_directory);

		// Snapshot all TTL at this current time point
		std::list<Command> expires;

		for (const auto &cmd : commands) {
			auto key = split_by_space(cmd.args()).front();
			auto ttl = db.cmdTTL(key);
			if (ttl.has_value()) {
				expires.emplace_back(KeyspaceCommand::EXPIRE,
									 fmt::format("{} {}", key, *ttl));
			} else if (ttl.error() == TTLError::KEY_ITER_NOTFOUND) continue;
			cmd.binarySerializeTo(of);
		}
		for (auto &cmd : expires) cmd.binarySerializeTo(of);
		of.close();
		return;
	}

	int status = 0;
	waitpid(rc, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return false;

	// rename old snapshot to back up, rename new snapshot, then remove back up
	std::unique_lock<std::shared_mutex> lck{file_mtx};
	fs::perms perms = fs::status(filename).permissions();
	bool can_read   = (perms & fs::perms::owner_read) != fs::perms::none;
	if (can_read) {
		filename.replace_extension(".bak");
		return failed("Unable to rename temporary snapshot",
					  std::errc::permission_denied);
	}

	fs::rename(tmp_filename, filename);
	fs::remove(filename / ".bak");
}
#endif

ErrorOr<std::unique_ptr<Db>> Snapshot::restoreSnapshot() {
	std::unique_lock<std::shared_mutex> lck_file(file_mtx);

	std::ifstream ifile(PATH, std::ios::in | std::ios::binary);
	if (!ifile.is_open())
		return failed("No snapshot file", std::errc::no_such_file_or_directory);

	cereal::BinaryInputArchive iarchive(ifile);
	std::list<Command> cmds;
	auto db = std::make_unique<Db>();
	while (true) {
		Command x;
		iarchive(x);
		cmds.push_back(x);
		// TODO: Handle error here
		TRY(db->execute(x));
	}

	std::unique_lock<std::shared_mutex> lck_cmd(cmds_mtx);
	this->clear();
	commands = std::move(cmds);
	return db;
}
