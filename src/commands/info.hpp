#pragma once

#include "core/database.hpp"
#include "resp/value.hpp"

#include <string_view>

namespace redis {
struct CommandInfo {
	using handle_func = resp::Value (*)(Database &, const resp::Value::Array &);

	std::string_view name;
	handle_func func; // Function to execute
	int arity;        // Required arity (negative = min args)
};
} // namespace redis
