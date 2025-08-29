#include "core/resp/value.hpp"

namespace redis {
class Database;
resp::Value handle_exists(Database &db, const resp::Value::Array &v);
} // namespace redis
