#pragma once

#include "commands/cmd.hpp"

#include <filesystem>
#include <list>
#include <memory>
#include <shared_mutex>

class Db;
static const std::filesystem::path PATH = "ledis.snap";

std::filesystem::path createTempFile();

// TODO: Rename this class. as its main fuctionality is to save log to disk
class Snapshot {
public:
	Snapshot(std::list<Command> list) : commands(std::move(list)) {}

	Snapshot() = default;

	void clear();

	void addCommand(Command command);

	ErrorOr<void> createFrom(Db &db);


	ErrorOr<Db> restoreSnapshot();

private:
	std::list<Command> commands;
	std::shared_mutex cmds_mtx;
	std::shared_mutex file_mtx;
};
