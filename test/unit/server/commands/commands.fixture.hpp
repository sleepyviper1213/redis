#pragma once
#include "support/temp_file.hpp"
#include "common/resp/value.hpp"
#include "server/commands.hpp"
#include "server/commands/reply.hpp"
#include "server/persistence/snapshot_manager.hpp"
#include "common/store/database.hpp"
#include "server/stats.hpp"

#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace redis::test {

/// Everything a command needs, in-process: no sockets, no event loop.
struct Server {
	TempFile dump;
	Database db;
	SnapshotManager persistence{dump.path()};
	ServerStats stats;
	CommandContext ctx{.db                 = db,
					   .persistence        = persistence,
					   .config_set_enabled = true,
					   .stats              = &stats};

	/// Run one command and return the RESP-encoded reply, exactly the bytes
	/// a client would receive.
	std::string run(std::vector<std::string> argv) {
		static const CommandHandler handler;
		return handler.execute(ctx, resp::Value::from_raw_array(argv));
	}
};

} // namespace redis::test
