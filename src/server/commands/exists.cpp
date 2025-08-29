#include "server/commands/exists.hpp"

namespace redis {
using resp::Value;

Value handle_exists(Database &db, const Value::Array &args) {
	if (args.size() < 2)
		return Value::from_simple_error("wrong number of arguments for EXISTS");

	int64_t count = 0;
	for (size_t i = 1; i < args.size(); ++i) {
		const auto &key = args[i].as_string();
		if (db.contains_key(key)) ++count;
	}
	return Value::from_integer(count);
}


} // namespace redis
