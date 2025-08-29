#pragma once
#include "core/resp/value.hpp"

namespace redis {
class Database;

resp::Value handle_ttl(Database &db, const resp::Value::Array &v);
resp::Value handle_pttl(Database &db, const resp::Value::Array &v);
} // namespace redis
