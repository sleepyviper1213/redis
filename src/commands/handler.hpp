#pragma once
#include "core/logging.hpp"
#include "resp.hpp"

#include <string>
#include <string_view>

namespace redis {
class Database;

class CommandHandler {
public:
	std::string handle_query(Database &db, std::string_view command) const;

	resp::Value execute_command(Database &db, const std::string &command,
								const resp::Value::Array &args) const;

private:
	CLASS_LOGGER(CommandHandler);
};
} // namespace redis
