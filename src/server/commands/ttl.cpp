#include "server/commands/ttl.hpp"

#include "core/error/try.hpp"
#include "core/logging.hpp"
#include "server/memory/database.hpp"

namespace redis {
using resp::Value;

Value handle_ttl(Database &db, const Value::Array &args) {
	const auto logger_    = Logger::get_instance().get("CommandHandler");
	const auto &key       = args[1].as_string();
	const auto expiration = TRY_VALUE(db.deadline_timer_of(key));
	return Value::from_integer(expiration.seconds_left());
}

Value handle_pttl(Database &db, const Value::Array &args) {
	const auto logger_    = Logger::get_instance().get("CommandHandler");
	const auto &key       = args[1].as_string();
	const auto expiration = TRY_VALUE(db.deadline_timer_of(key));
	return Value::from_integer(expiration.millis_left());
}

} // namespace redis
