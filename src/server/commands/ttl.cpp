#include "server/commands/ttl.hpp"

#include "core/error/try.hpp"
#include "core/utils/get_class_logger.hpp"
#include "server/memory/database.hpp"

namespace redis {
using resp::Value;

Value handle_ttl(Database &db, const Value::Array &args) {
	const auto logger_    = make_logger("CommandHandler");
	const auto &key       = args[1].as_string();
	const auto expiration = TRY_VALUE(db.deadline_timer_of(key));
	return Value::from_integer(expiration.seconds_left());
}

Value handle_pttl(Database &db, const Value::Array &args) {
	const auto logger_    = make_logger("CommandHandler");
	const auto &key       = args[1].as_string();
	const auto expiration = TRY_VALUE(db.deadline_timer_of(key));
	return Value::from_integer(expiration.millis_left());
}

} // namespace redis
