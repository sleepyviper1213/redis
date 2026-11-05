#pragma once
#include "executor.hpp"
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/persistence/snapshot_manager.hpp"
#include "server/storage/database.hpp"

#include <filesystem>
#include <optional>
#include <utility>

namespace redis {

/**
 * \brief What the server and all of its sessions share: the keyspace, how it
 * is saved, the command table and the thread that executes commands.
 *
 * One of these, many users: RedisServer, the Listener and every session hold
 * it through a std::shared_ptr, so it lives until the last of them is gone and
 * none of them can outlive what they use.
 */
struct Services {
	/// \param aof_config Log write commands to an append-only file; if not
	///        set, the server persists with snapshots alone.
	Services(std::filesystem::path dump_file, bool threaded,
			 std::optional<AofConfig> aof_config = std::nullopt)
		: persistence(dump_file), keyspace(threaded) {
		if (aof_config) aof.emplace(std::move(*aof_config), std::move(dump_file));
	}

	Database db;
	SnapshotManager persistence;
	CommandHandler handler;
	/// Gets its aof once the log has been replayed; see AppendOnlyFile::load.
	CommandContext ctx{db, persistence};
	std::optional<AppendOnlyFile> aof;
	/// Declared last so it is destroyed first: its thread, the only one
	/// touching the keyspace, is joined before the keyspace goes away.
	Executor keyspace;
};

} // namespace redis
