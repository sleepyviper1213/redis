#pragma once
#include "args.hpp"
#include "common/resp/value.hpp"
#include "common/store/database_fwd.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace redis {
class SnapshotManager;
class AppendOnlyFile;
class NetGuard;
class ServerStats;

/// Everything a command may act on. A borrow: the owner (Services) keeps what
/// it points to alive and never rebinds or assigns the context.
struct CommandContext {
	// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
	Database &db;
	SnapshotManager &persistence;
	// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
	/// The append-only file the write commands are logged to; null when the
	/// server runs without one, and while a log is being replayed.
	AppendOnlyFile *aof = nullptr;
	/// Whether CONFIG SET may change settings. Off unless the operator asks
	/// (--enable-config-set): any client could otherwise change them.
	bool config_set_enabled = false;
	/// Counted by the dispatcher and reported by INFO; null in a bare context.
	ServerStats *stats = nullptr;
	/// Connection counts and limits for INFO; null when there is no network.
	const NetGuard *guard = nullptr;
};

/// Runs one command and returns its reply, which the dispatcher writes with
/// `fmt::format("{:e}", reply)`. A command returns only once its outcome is
/// known, so a failure part-way is still an error reply.
using CommandFn = resp::Value (*)(CommandContext &, Args);

/// The replies commands share.
namespace reply {
inline resp::Value ok() { return resp::Value::from_simple_string("OK"); }

template <typename... Args>
resp::Value simple(fmt::format_string<Args...> text, Args &&...args) {
	return resp::Value::from_simple_string(
		fmt::format(text, std::forward<Args>(args)...));
}

template <typename... Args>
resp::Value error(fmt::format_string<Args...> message, Args &&...args) {
	return resp::Value::from_simple_error(
		fmt::format(message, std::forward<Args>(args)...));
}

inline resp::Value integer(int64_t n) { return resp::Value::from_integer(n); }

inline resp::Value nil() { return resp::Value::from_null(); }

/// Nil where an array was asked for: *-1, not $-1.
inline resp::Value nil_array() { return resp::Value::from_null_array(); }

inline resp::Value bulk(std::string_view text) {
	return resp::Value::from_bulk_string(std::string(text));
}

inline resp::Value array(resp::Value::Array elements) {
	return resp::Value::from_array(std::move(elements));
}

inline resp::Value wrong_type() {
	return error("WRONGTYPE Operation against a key holding the wrong kind of "
				 "value");
}

inline resp::Value not_integer() {
	return error("ERR value is not an integer or out of range");
}

inline resp::Value syntax_error() { return error("ERR syntax error"); }
} // namespace reply

struct CommandInfo {
	std::string_view name; ///< Upper case, as looked up.
	CommandFn fn;
	/// Counts the command name, Redis-style: N means exactly N arguments,
	/// -N means at least N.
	int arity;
	/// Changes the keyspace: logged to the append-only file when it succeeds.
	/// A command that is not deterministic (it reads the clock, picks at
	/// random) must say what to log instead, with AppendOnlyFile::replace().
	bool write = false;
};

/// Command tables, one per family. Each lives with its implementations.
std::span<const CommandInfo> string_commands();
std::span<const CommandInfo> keyspace_commands();
std::span<const CommandInfo> list_commands();
std::span<const CommandInfo> set_commands();
std::span<const CommandInfo> hash_commands();
std::span<const CommandInfo> server_commands();

} // namespace redis
