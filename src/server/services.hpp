#pragma once
#include "commands/command.hpp"
#include "commands/handler.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep
#include "net/executor.hpp"
#include "net/guard.hpp"
#include "persistence.hpp"
#include "stats.hpp"

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
			 std::optional<AofConfig> aof_config = std::nullopt,
			 NetLimits limits                    = {},
			 bool config_set_enabled             = false)
		: persistence(dump_file), // copies dump_file; move is below
		  guard(limits),
		  keyspace(threaded) {
		ctx.config_set_enabled = config_set_enabled;
		ctx.stats              = &stats;
		ctx.guard              = &guard;
		if (aof_config)
			aof.emplace(std::move(*aof_config), std::move(dump_file));
	}

	Database db;
	ServerStats stats;
	SnapshotManager persistence;
	CommandHandler handler;
	/// Gets its aof once the log has been replayed; see AppendOnlyFile::load.
	CommandContext ctx{.db = db, .persistence = persistence};
	std::optional<AppendOnlyFile> aof;
	/// Admission control and buffer limits for every client; see NetGuard.
	NetGuard guard;
	/// Declared last so it is destroyed first: its thread, the only one
	/// touching the keyspace, is joined before the keyspace goes away.
	Executor keyspace;
};

} // namespace redis
