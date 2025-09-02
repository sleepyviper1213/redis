#pragma once
#include "core/logging.hpp"
#include "core/resp.hpp"
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


	void register_command(std::string_view name,
						  CommandInfo::handle_func handler, int arity);

protected:
	resp::Value execute_command(Database &db, const std::string &command,
								const resp::Value::Array &args) const;

private:
	std::vector<CommandInfo> command_map_;
	CLASS_LOGGER(CommandHandler);
};
} // namespace redis
