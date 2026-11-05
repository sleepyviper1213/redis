#pragma once
#include "reply.hpp"
#include "server/storage/database_fwd.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace redis {
class SnapshotManager;
class AppendOnlyFile;

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
};

/// A command's arguments; args[0] is the command name itself. They view the
/// client's input buffer, so a command that keeps one must copy it.
using Args = std::span<const std::string_view>;

/// Runs one command, writing its reply to the writer.
using CommandFn = void (*)(CommandContext &, Args, ReplyWriter &);

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

/// Parse a decimal int64 argument the way Redis does: no spaces, no '+'.
[[nodiscard]] std::optional<int64_t> parse_int(std::string_view s) noexcept;

} // namespace redis
