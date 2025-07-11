#include "gate.hpp"

#include <spdlog/spdlog.h>

std::expected<Ret, AppError> Gate::parseAndExecute(const std::string &cmd) {
	auto command = Command::fromString(cmd);
	if (!command.has_value()) return std::unexpected(command.error());

	// TODO: Pattern matching
	// if (command->type() == CommandType::EXIT)
	// return std::unexpected(AppError::EXIT);
	if (command->type() == CommandType::SNAPSHOT) {
		spdlog::info("[COMMAND] Save");
		if (!command->args().empty())
			return std::unexpected(ExecuteError::INVALID_ARGUMENTS);
		if (!snapshot->createSnapshot(*db))
			return std::unexpected(ExecuteError::SNAPSHOT_CREATION_FAILED);
	} else if (command->type() == CommandType::RESTORE) {
		spdlog::info("[COMMAND] Restore");
		auto tmpDb = snapshot->restoreSnapshot();
		if (!tmpDb.has_value()) return std::unexpected(tmpDb.error());
		db.reset(*tmpDb);
	} else {
		auto y = db->execute(*command);
	}

	snapshot->addCommand(*command);
	return *y;
}
