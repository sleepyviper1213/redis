#include "resp/value.hpp"

namespace redis {
class Database;
resp::Value handle_ping(Database &db, const resp::Value::Array &v);
} // namespace redis
