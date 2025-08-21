#include "cmd.hpp"

#include "database.hpp"

namespace redis {
RedisCommand::RedisCommand(std::string n, CommandHandler h, int a,
						   std::string f)
	: name_(std::move(n)),
	  handler(std::move(h)),
	  arity(a),
	  flags(std::move(f)) {}

resp::Value RedisCommand::execute(Database &db,
								  const std::vector<std::string> &argv) const {
	if (arity > 0 && (int)argv.size() != arity) {
		return resp::Value::from_simple_error(
			fmt::format("-ERR wrong number of arguments for '{}' command",
						name_));
	}
	return handler(db, argv);
}
} // namespace redis
