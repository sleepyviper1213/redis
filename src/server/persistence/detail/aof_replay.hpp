#pragma once
#include "server/persistence/persist_error.hpp"

#include <cstddef>
#include <expected>
#include <string_view>

namespace redis {
class CommandHandler;
struct CommandContext;
} // namespace redis

namespace redis::persistence::detail {

/**
 * \brief Run the commands in \p data, the bytes of one incremental file, in
 * order through \p handler; their replies are dropped (DX-92).
 *
 * Only the bytes: AppendOnlyFile::replay opens, reads and cuts the file, so
 * this loop runs in-process with no file at all (the AOF fuzz harness,
 * TC-331).
 *
 * \p commands goes up by one per command run, also when an error follows.
 * \return The length of the whole commands: `data.size()`, or less when the
 * bytes end in the middle of a command (the server died writing it), which is
 * not run. Otherwise LogCorrupt, `at` the offset of the request that does not
 * parse and `code` the parse error, with `path` left for the caller; the
 * commands before it have run.
 */
[[nodiscard]] std::expected<size_t, PersistError>
replay_log(std::string_view data, CommandContext &ctx,
		   const CommandHandler &handler, size_t &commands);

} // namespace redis::persistence::detail
