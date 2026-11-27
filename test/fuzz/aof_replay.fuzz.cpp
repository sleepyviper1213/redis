// Fuzzes the replay of an append-only log, persistence::detail::replay_log
// (TC-331, DX-92).
//
// The input is the contents of the last incremental file. It is replayed as
// AppendOnlyFile::load replays it, into an empty keyspace with expiry off,
// except that the commands that write files or fork (SAVE, BGSAVE,
// BGREWRITEAOF) and CONFIG are stubbed: every other command runs on whatever
// arguments the fuzzer frames, under ASan and UBSan.
//
// Checked: the replay ends at a whole command (all of the input, or before a
// torn last one) or fails with LogCorrupt inside the input; replaying only the
// bytes before that point, into a new keyspace, ends at the same place
// without error and runs as many commands.

#include "common/resp/parser.hpp"
#include "common/resp/request.hpp"
#include "common/resp/value.hpp"
#include "common/store/database.hpp"
#include "core/error/verify.hpp"
#include "server/commands/args.hpp"
#include "server/commands/command.hpp"
#include "server/commands/handler.hpp"
#include "server/persistence/detail/aof_replay.hpp"
#include "server/persistence/persist_error.hpp"
#include "server/persistence/snapshot_manager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace {
using redis::Args;
using redis::CommandContext;
using redis::CommandInfo;
using redis::persistence::PersistError;
using redis::persistence::PersistKind;

redis::resp::Value not_while_fuzzing(CommandContext &, Args) {
	return redis::reply::error("ERR not run while fuzzing");
}

/// They write files, fork, or change the server's settings.
constexpr std::array STUBS{
	CommandInfo{.name = "SAVE", .fn = not_while_fuzzing, .arity = -1},
	CommandInfo{.name = "BGSAVE", .fn = not_while_fuzzing, .arity = -1},
	CommandInfo{.name = "BGREWRITEAOF", .fn = not_while_fuzzing, .arity = -1},
	CommandInfo{.name = "CONFIG", .fn = not_while_fuzzing, .arity = -1},
};

struct Replayed {
	std::expected<size_t, PersistError> end;
	size_t commands = 0;
};

Replayed replay(std::string_view log) {
	static const redis::CommandHandler handler{STUBS};
	// Never saves: SAVE and BGSAVE are stubbed.
	static redis::SnapshotManager persistence{"fuzz-unused.snap"};

	redis::Database db;
	db.set_expiry_enabled(false);
	CommandContext ctx{.db = db, .persistence = persistence};
	Replayed out;
	out.end = redis::persistence::detail::replay_log(log, ctx, handler,
													 out.commands);
	return out;
}

/// Where a replay stopped: the end of the whole commands, or the bad request.
size_t stop_of(const Replayed &replayed) {
	if (replayed.end) return *replayed.end;
	return static_cast<size_t>(replayed.end.error().at);
}

void check_stop(std::string_view log, const Replayed &replayed) {
	const size_t stop = stop_of(replayed);
	if (!replayed.end) {
		REDIS_VERIFY(replayed.end.error().kind == PersistKind::LogCorrupt,
					 "replay failed with another error than LogCorrupt");
		REDIS_VERIFY(stop < log.size(), "corruption reported past the end");
		return;
	}
	REDIS_VERIFY(stop <= log.size(), "the whole commands end past the input");
	if (stop == log.size()) return;

	// What is left is one torn command: the framing wants more bytes.
	std::vector<std::string_view> args;
	std::deque<std::string> owned;
	size_t pos      = stop;
	const auto rest = redis::resp::parse_request(log, pos, args, owned);
	REDIS_VERIFY(!rest && redis::resp::Parser::is_incomplete(rest.error()),
				 "replay stopped before a command it could have run");
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	const std::string_view log(reinterpret_cast<const char *>(data), size);
	const Replayed whole = replay(log);
	check_stop(log, whole);

	const size_t stop       = stop_of(whole);
	const Replayed before   = replay(log.substr(0, stop));
	REDIS_VERIFY(before.end.has_value() && *before.end == stop,
				 "the bytes before the stop do not replay to the stop");
	REDIS_VERIFY(before.commands == whole.commands,
				 "the bytes before the stop ran another number of commands");
	return 0;
}
