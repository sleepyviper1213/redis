#include "gate.hpp"

#include <spdlog/spdlog.h>

ErrorOr<Ret> Gate::parseAndExecute(const std::string &cmd) {
	auto command = TRY(Command::fromString(cmd));

	// TODO: Pattern matching
	// if (command->type() == CommandType::EXIT)
	// return std::unexpected(AppError::EXIT);
	if (command.type() == CommandType::SNAPSHOT) {
		spdlog::info("[COMMAND] Save");
		if (!command.args().empty())
			return failed("Save command takes no argument",
						  std::errc::invalid_argument);
		if (!snapshot->createSnapshot(*db))
			// TODO this error code is correct?
			return failed("Unable to create snapshot", std::errc::interrupted);
	} else if (command.type() == CommandType::RESTORE) {
		spdlog::info("[COMMAND] Restore");
		auto tmpDb = TRY(snapshot->restoreSnapshot());
		db         = std::move(tmpDb);
	} else {
		auto y = db->execute(command);
	}

	snapshot->addCommand(command);
	// TODO
	return *y;
}
