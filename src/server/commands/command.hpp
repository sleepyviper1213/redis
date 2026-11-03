#pragma once
#include "reply.hpp"
#include "server/storage/database_fwd.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace redis {
class Persistence;

/// Everything a command may act on. A borrow: the owner (Services) keeps both
/// alive and never rebinds or assigns the context.
struct CommandContext {
	// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
	Database &db;
	Persistence &persistence;
	// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
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
