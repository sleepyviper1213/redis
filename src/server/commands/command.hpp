#pragma once
#include "core/resp/value.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace redis {
class Database;
class Persistence;

/// Everything a command may act on.
struct CommandContext {
	Database &db;
	Persistence &persistence;
};

/// A command's arguments; args[0] is the command name itself. They view the
/// client's input buffer, so a command that keeps one must copy it.
using Args = std::span<const std::string_view>;

using CommandFn = resp::Value (*)(CommandContext &, Args);

struct CommandInfo {
	std::string_view name; ///< Upper case, as looked up.
	CommandFn fn;
	/// Counts the command name, Redis-style: N means exactly N arguments,
	/// -N means at least N.
	int arity;
};

/// Command tables, one per family. Each lives with its implementations.
std::span<const CommandInfo> string_commands();
std::span<const CommandInfo> keyspace_commands();
std::span<const CommandInfo> list_commands();
std::span<const CommandInfo> set_commands();
std::span<const CommandInfo> hash_commands();
std::span<const CommandInfo> server_commands();

// Replies and argument helpers shared by the command families.
namespace reply {
inline resp::Value ok() { return resp::Value::from_simple_string("OK"); }
inline resp::Value nil() { return resp::Value::from_null(); }
inline resp::Value integer(int64_t n) { return resp::Value::from_integer(n); }
inline resp::Value bulk(std::string s) {
	return resp::Value::from_bulk_string(std::move(s));
}
inline resp::Value error(std::string message) {
	return resp::Value::from_simple_error(std::move(message));
}
inline resp::Value wrong_type() {
	return error(
		"WRONGTYPE Operation against a key holding the wrong kind of value");
}
inline resp::Value not_integer() {
	return error("ERR value is not an integer or out of range");
}
inline resp::Value syntax_error() { return error("ERR syntax error"); }
} // namespace reply

/// Parse a decimal int64 argument the way Redis does: no spaces, no '+'.
[[nodiscard]] std::optional<int64_t> parse_int(std::string_view s) noexcept;

} // namespace redis
