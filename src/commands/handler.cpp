#include "commands/handler.hpp"

#include "redis_cmds.hpp"
#include "utils/toupper.hpp"

namespace redis {
using namespace resp;

std::string CommandHandler::handle_query(Database &db,
										 std::string_view query) const {
	const auto cmd = Parser::parse(query);
	if (!cmd.has_value()) return fmt::format("ERR {}", cmd.error());

	if (!cmd->is_array()) return "Expected an array for command";

	logger_->trace("Query: {:?}", *cmd);
	auto args                      = cmd->as_array();
	const std::string command_name = ascii_toupper(args[0].as_string());
	const Value response           = execute_command(db, command_name, args);
	const std::string resp         = fmt::format("{:e}", response);
	logger_->trace("Response: {:?}", response);
	return resp;
}

Value CommandHandler::execute_command(Database &db, const std::string &command,
									  const Value::Array &args) const {
	const auto *x = gperf::in_word_set(command.data(), command.size());
	if (x == nullptr)
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
