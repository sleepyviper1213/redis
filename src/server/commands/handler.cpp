#include "handler.hpp"

#include "core/logging.hpp"
#include "core/resp/value.hpp"
#include "core/utils/toupper.hpp"
#include "server/commands/command.hpp"
#include "server/commands/reply.hpp"
#include "server/diagnostics/current_command.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/storage/database.hpp"

#include <fast_float/fast_float.h> // IWYU pragma: keep (integer from_chars overload)
#include <fmt/format.h>
#include <fmt/ranges.h> // IWYU pragma: keep (fmt::join / range formatting)
#include <spdlog/logger.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::commands);
}
} // namespace

using resp::Value;

std::optional<int64_t> parse_int(std::string_view s) noexcept {
	int64_t value = 0;
	const auto [ptr, ec] =
		fast_float::from_chars(s.data(), s.data() + s.size(), value);
	if (ec != std::errc() || ptr != s.data() + s.size() || s.empty())
		return std::nullopt;
	return value;
}

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

const CommandInfo *CommandHandler::dispatch(CommandContext &ctx, Args args,
											ReplyWriter &out) const {
	if (args.empty()) {
		out.error("ERR Protocol error: expected a non-empty array of bulk "
				  "strings");
		return nullptr;
	}

	std::array<char, MAX_COMMAND_NAME> name_buf{};

	const auto it = commands_.find(upper_name(args[0], name_buf));
	if (it == commands_.end()) {
		out.error(fmt::format("ERR unknown command '{}'", args[0]));
		return nullptr;
	}

	const CommandInfo &info = it->second;
	const auto argc         = static_cast<int>(args.size());
	const bool arity_ok =
		info.arity >= 0 ? argc == info.arity : argc >= -info.arity;
	if (!arity_ok) {
		out.error(fmt::format("ERR wrong number of arguments for '{}' command",
							  tolower(std::string{info.name})));
		return nullptr;
	}
	// Debug builds: the thread that runs commands owns the keyspace.
	ctx.db.claim_owner();
	// For a crash report: which command was running when the process died.
	const diagnostics::CommandScope running{info.name, args.size()};
	info.fn(ctx, args, out);
	return &info;
}

void CommandHandler::execute(CommandContext &ctx, Args args,
							 std::string &out) const {
	spdlog::logger &logger = log();
	logger.trace("Query: {::?}", args);

	const size_t reply_start = out.size();
	ReplyWriter writer(out);
	const CommandInfo *ran = nullptr;
	try {
		ran = dispatch(ctx, args, writer);
	} catch (const std::bad_alloc &) {
		// The command may have written part of its reply, and changed part of
		// what it was asked to (an MSET, say). Only the reply can be taken
		// back: answer this request with an error, so the client and the rest of
		// its pipeline stay in step, instead of ending the connection.
		out.resize(reply_start);
		writer.error("OOM command not allowed: out of memory");
		logger.error("Out of memory running '{}'",
					 args.empty() ? std::string_view{} : args[0]);
	}

	// A write that failed changed nothing, so there is nothing to replay. One
	// that "succeeded" without changing anything (DEL of a missing key) is
	// logged anyway: replaying it is harmless, and telling the two apart
	// would need every command to report it.
	if (ctx.aof != nullptr) {
		const bool failed = out.size() > reply_start && out[reply_start] == '-';
		ctx.aof->command_ran(
			ctx.db, ran != nullptr && ran->write && !failed, args);
	}

	logger.trace("Response: {:?}", std::string_view(out).substr(reply_start));
}

} // namespace redis
