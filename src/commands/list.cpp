#include "commands/list.hpp"

#include "memory/database.hpp"

#include <ranges>

namespace redis {

using resp::Value;

Value handle_list(Database &db, const Value::Array &args) {
	// const auto &key = args[1].as_string();
	// auto x = db.insert_or_append_range(key, args | std::views::drop(2));
	// if (!x.has_value())
	// return Value::from_simple_error(fmt::format("{}", x.error()));
	// return Value::from_integer(static_cast<int64_t>(
	// db.get_value_by_key(key)->get().as_array().size()));
}
} // namespace redis
