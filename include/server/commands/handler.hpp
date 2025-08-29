#pragma once
#include "core/resp.hpp"
#include "core/utils/get_class_logger.hpp"
#include "server/commands/info.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace redis {
class Database;

class CommandHandler {
public:
	CommandHandler();
	std::string handle_query(Database &db, std::string_view command) const;


	void registerCommand(std::string_view name,
						 CommandInfo::handle_func handler, int arity);

protected:
	resp::Value executeCommand(Database &db, const std::string &command,
							   const resp::Value::Array &args) const;

private:
	std::vector<CommandInfo> command_map;
	CLASS_LOGGER(CommandHandler);
};
} // namespace redis
