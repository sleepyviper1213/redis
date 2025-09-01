#include "core/resp/value.hpp"

namespace redis {
class Database;
resp::Value handle_list(Database &db, const resp::Value::Array &v);
} // namespace redis
