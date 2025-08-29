#include "resp/value.hpp"

namespace redis {
class Database;
resp::Value handle_save(Database &db, const resp::Value::Array &v);
} // namespace redis
