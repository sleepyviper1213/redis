#include "server/commands/list.hpp"

namespace redis {
class Database;

resp::Value handle_list(Database &db, const resp::Value::Array &v) {}
} // namespace redis
