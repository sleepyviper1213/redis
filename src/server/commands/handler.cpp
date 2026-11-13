#include "handler.hpp"

#include "server/log_channels.hpp"
#include "common/resp/value.hpp"
#include "core/chrono/elapsed_timer.hpp"
#include "core/chrono/tick_clock.hpp"
#include "core/utils/ascii.hpp"
#include "command.hpp"
#include "server/diagnostics/current_command.hpp"
#include "server/persistence/append_only_file.hpp"
#include "common/store/database.hpp"
#include "server/stats.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h> // IWYU pragma: keep (fmt::join / range formatting)
#include <spdlog/logger.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::commands);
}
} // namespace

using resp::Value;

CommandHandler::CommandHandler() : CommandHandler(std::span<const CommandInfo>{}) {}

CommandHandler::CommandHandler(std::span<const CommandInfo> extra) {
	for (const auto family : {string_commands(),
							  keyspace_commands(),
							  list_commands(),
							  set_commands(),
							  hash_commands(),
							  server_commands(),
							  extra})
		for (const CommandInfo &info : family)
			commands_.insert_or_assign(std::string(info.name), info);
}

namespace {
// Longer than any command name: a longer one cannot match, so it is not
// looked up at all.
constexpr size_t MAX_COMMAND_NAME = 32;

// Upper-cases an ASCII command name into `buf`, so the lookup needs no
// allocation. Returns an empty view (which matches no command) when the name
// does not fit.
std::string_view upper_name(std::string_view name,
							std::array<char, MAX_COMMAND_NAME> &buf) {
	if (name.size() > buf.size()) return {};
	for (size_t i = 0; i < name.size(); ++i) {
		const char c = name[i];
		buf[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
	}
	return {buf.data(), name.size()};
}
} // namespace

std::string CommandHandler::execute(CommandContext &ctx,
									const Value &request) const {
	std::string out;
	execute(ctx, request, out);
	return out;
}

void CommandHandler::execute(CommandContext &ctx, const Value &request,
							 std::string &out) const {
	std::vector<std::string_view> args;
	// Not Value::is_string(): a null Value holds an empty string too.
	const bool well_formed =
		request.is_array() &&
		std::ranges::all_of(request.as_array(), [](const Value &arg) {
			return arg.is_bulk_string() || arg.is_simple_string();
		});
	if (well_formed)
		for (const Value &arg : request.as_array())
			args.push_back(arg.as_string());
	execute(ctx, args, out);
}

CommandHandler::Dispatched CommandHandler::dispatch(CommandContext &ctx,
													Args args) const {
	if (args.empty()) {
		return {.reply=reply::error("ERR Protocol error: expected a non-empty array of "
							 "bulk strings"),
				.ran=nullptr};
	}

	std::array<char, MAX_COMMAND_NAME> name_buf{};

	const auto it = commands_.find(upper_name(args[0], name_buf));
	if (it == commands_.end()) {
		// Redis' wording: the name, then the first arguments, each quoted and
		// followed by a space, until 128 bytes of them have been echoed.
		constexpr size_t MAX_ECHOED = 128;
		std::string echoed;
		for (const std::string_view arg : args.subspan(1)) {
			if (echoed.size() >= MAX_ECHOED) break;
			fmt::format_to(std::back_inserter(echoed),
						   "'{}' ",
						   arg.substr(0, MAX_ECHOED - echoed.size()));
		}
		return {.reply=reply::error(
					"ERR unknown command '{}', with args beginning with: {}",
					args[0].substr(0, MAX_ECHOED),
					echoed),
				.ran=nullptr};
	}

	const CommandInfo &info = it->second;
	const auto argc         = static_cast<int>(args.size());
	const bool arity_ok =
		info.arity >= 0 ? argc == info.arity : argc >= -info.arity;
	if (!arity_ok) {
		return {.reply=reply::error("ERR wrong number of arguments for '{}' command",
							 ascii_lowered(info.name)),
				.ran=nullptr};
	}
	// Debug builds: the thread that runs commands owns the keyspace.
	ctx.db.claim_owner();
	// For a crash report: which command was running when the process died.
	const diagnostics::CommandScope running{info.name, args.size()};
	if (ctx.stats == nullptr) return {.reply=info.fn(ctx, args), .ran=&info};
	// TickClock, not steady_clock: it is read twice per command, and on macOS
	// steady_clock costs about a tenth of a pipelined SET.
	const BasicElapsedTimer<TickClock> timer;
	Value result = info.fn(ctx, args);
	ctx.stats->command_ran(info.name, timer.duration_elapsed());
	return {.reply=std::move(result), .ran=&info};
}

void CommandHandler::execute(CommandContext &ctx, Args args,
							 std::string &out) const {
	spdlog::logger &logger = log();
	// Only the command's name and size, never its arguments or reply: those are
	// the clients' data (keys, values, and anything else they send), and a log
	// file is read and kept by other people.
	logger.trace("Query: {} ({} args)",
				 args.empty() ? std::string_view{} : args[0],
				 args.size());

	const size_t reply_start = out.size();
	const CommandInfo *ran = nullptr;
	bool failed            = false;
	try {
		const auto [response, command] = dispatch(ctx, args);
		fmt::format_to(std::back_inserter(out), "{:e}", response);
		ran    = command;
		failed = response.is_error();
	} catch (const std::bad_alloc &) {
		// The command may have changed part of what it was asked to (an MSET,
		// say), and formatting its reply may have added part of it to `out`.
		// Only the reply can be taken back: answer this request with an error,
		// so the client and the rest of its pipeline stay in step, instead of
		// ending the connection. Don't format it: that allocates and would
		// throw bad_alloc again.
		out.resize(reply_start);
		out += "-OOM command not allowed: out of memory\r\n";
		logger.error("Out of memory running '{}'",
					 args.empty() ? std::string_view{} : args[0]);
	}

	// A write that failed changed nothing, so there is nothing to replay. One
	// that "succeeded" without changing anything (DEL of a missing key) is
	// logged anyway: replaying it is harmless, and telling the two apart
	// would need every command to report it.
	if (ctx.aof != nullptr) {
		ctx.aof->command_ran(
			ctx.db, ran != nullptr && ran->write && !failed, args);
	}

	logger.trace("Response: {} bytes", out.size() - reply_start);
}

} // namespace redis
