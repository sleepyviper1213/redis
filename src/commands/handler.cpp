#include "handler.hpp"

#include <spdlog/logger.h>

#include "crud.hpp"
#include "exists.hpp"
#include "ping.hpp"
#include "ttl.hpp"
#include "utils/toupper.hpp"

#include <algorithm>

namespace redis {
using resp::Parser;
using resp::Value;

CommandHandler::CommandHandler()
	: command_map{{"SET", handle_set, 3},
				  {"GET", handle_get, 2},
				  {"PING", handle_ping, 1},
				  {"DEL", handle_del, 2},
				  {"EXISTS", handle_exists, 2},
				  {"TTL", handle_ttl, 2},
				  {"PTTL", handle_pttl, 2}} {}

void CommandHandler::registerCommand(std::string_view name,
									 CommandInfo::handle_func handler,
									 int arity) {
	command_map.emplace_back(name, handler, arity);
}

std::string CommandHandler::handle_query(Database &db,
										 std::string_view query) const {
	const auto cmd = Parser::parse(query);
	if (!cmd.has_value()) return fmt::format("ERR {}", cmd.error());

	if (!cmd->is_array()) return "Expected an array for command";

	logger_->trace("Query: {:?}", *cmd);
	auto args                      = cmd->as_array();
	const std::string command_name = toupper(args[0].as_string());
	const Value response           = executeCommand(db, command_name, args);
	const std::string resp         = fmt::format("{:e}", response);
	logger_->trace("Response: {:?}", response);
	return resp;
}

Value CommandHandler::executeCommand(Database &db, const std::string &command,
									 const Value::Array &args) const {
	auto x = std::ranges::find(command_map, command, &CommandInfo::name);
	if (x == command_map.end())
		return Value::from_simple_error(
			fmt::format("ERR unknown command '{}'", command));
	if (args.size() < x->arity)
		return Value::from_simple_error(
			fmt::format("ERR wrong number of arguments for {} command",
						command));

	auto fn = x->func;
	return fn(db, args);
}

} // namespace redis
