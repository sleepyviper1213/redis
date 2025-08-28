#include "ping.hpp"

#include "resp/value.hpp"

namespace redis {
using resp::Value;

resp::Value handle_ping(Database &db, const Value::Array &args) {
	if (args.size() == 1) return Value::from_simple_string("PONG");
	return args[1];
}
} // namespace redis
