#include "core/resp/value.hpp"

namespace redis {
resp::Value handle_exists(Database &db, const resp::Value::Array &v);
} // namespace redis
