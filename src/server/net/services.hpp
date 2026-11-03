#pragma once
#include "executor.hpp"
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/persistence/persistence.hpp"
#include "server/storage/database.hpp"

#include <filesystem>
#include <utility>

namespace redis {

/**
 * \brief What the server and all of its sessions share: the keyspace, how it
 * is saved, the command table and the thread that executes commands.
 *
 * One of these, many users: RedisServer, the TcpServer and every session hold
 * it through a std::shared_ptr, so it lives until the last of them is gone and
 * none of them can outlive what they use.
 */
struct Services {
	Services(std::filesystem::path dump_file, bool threaded)
		: persistence(std::move(dump_file)), keyspace(threaded) {}

	Database db;
	Persistence persistence;
	CommandHandler handler;
	CommandContext ctx{db, persistence};
	/// Declared last so it is destroyed first: its thread, the only one
	/// touching the keyspace, is joined before the keyspace goes away.
	Executor keyspace;
};

} // namespace redis
