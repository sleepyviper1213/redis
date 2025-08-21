#pragma once

#include "database.hpp"
#include "resp/value.hpp"

#include <fmt/format.h>

#include <functional>
#include <string>
#include <vector>

namespace redis {
struct Db; // forward declaration

class RedisCommand {
public:
	using CommandHandler = std::function<resp::Value(
		Database &, const std::vector<std::string> &)>;

	RedisCommand(std::string n, CommandHandler h, int a, std::string f);

	resp::Value execute(Database &db,
						const std::vector<std::string> &argv) const;

	[[nodiscard]] const std::string &name() const { return name_; }

private:
	std::string name_;      // Command name ("SET", "GET")
	CommandHandler handler; // Function to execute
	int arity;              // Required arity (negative = min args)
	std::string flags;      // "write", "readonly", "fast"...
};
} // namespace redis
