#pragma once

#include "commands/cmd.hpp"
#include "error/snapshot_error.hpp"

#include <filesystem>
#include <list>
#include <shared_mutex>

class Db;
static const std::filesystem::path PATH = "ledis.snap";

std::filesystem::path createTempFile();

class Snapshot {
public:
	void clear();

	void addCommand(Command command);

	bool createSnapshot(Db &db);


	std::expected<Db *, SnapshotError> restoreSnapshot();

private:
	std::list<Command> commands;
	std::shared_mutex cmds_mtx;
	std::shared_mutex file_mtx;
};
