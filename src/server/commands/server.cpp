// Connection and server commands: PING, ECHO, SAVE, BGSAVE, LASTSAVE,
// CONFIG GET.
#include "command.hpp"
#include "core/utils/glob.hpp"
#include "core/utils/toupper.hpp"
#include "server/persistence/persistence.hpp"
#include "server/storage/database.hpp"

#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace redis {
using resp::Value;

namespace {
Value ping(CommandContext &, Args args) {
	if (args.size() > 2)
		return reply::error("ERR wrong number of arguments for 'ping' command");
	if (args.size() == 1) return Value::from_simple_string("PONG");
	return reply::bulk(args[1].as_string());
}

Value echo(CommandContext &, Args args) {
	return reply::bulk(args[1].as_string());
}

Value save(CommandContext &ctx, Args) {
	const auto result = ctx.persistence.save(ctx.db);
	return result ? reply::ok() : reply::error(result.error());
}

Value bgsave(CommandContext &ctx, Args) {
	const auto result = ctx.persistence.background_save(ctx.db);
	return result ? Value::from_simple_string("Background saving started")
				  : reply::error(result.error());
}

Value lastsave(CommandContext &ctx, Args) {
	const auto unix_seconds =
		std::chrono::duration_cast<std::chrono::seconds>(
			ctx.persistence.last_save().time_since_epoch())
			.count();
	return reply::integer(unix_seconds);
}

// CONFIG GET <pattern>: read-only view of the settings clients commonly probe
// (redis-benchmark asks for "save" and "appendonly" on startup).
Value config(CommandContext &ctx, Args args) {
	if (toupper(args[1].as_string()) != "GET" || args.size() != 3)
		return reply::error("ERR CONFIG supports only 'CONFIG GET <pattern>'");

	const std::pair<std::string, std::string> parameters[] = {
		{"appendonly", "no"},
		{"dbfilename", ctx.persistence.file().filename().string()},
		{"dir", ctx.persistence.file().parent_path().string()},
		{"save", ""},
	};

	const std::string &pattern = args[2].as_string();
	Value::Array result;
	for (const auto &[name, value] : parameters) {
		if (!glob_match(pattern, name)) continue;
		result.push_back(reply::bulk(name));
		result.push_back(reply::bulk(value));
	}
	return Value::from_array(std::move(result));
}

constexpr std::array COMMANDS{
	CommandInfo{"PING", ping, -1},
	CommandInfo{"ECHO", echo, 2},
	CommandInfo{"SAVE", save, 1},
	CommandInfo{"BGSAVE", bgsave, 1},
	CommandInfo{"LASTSAVE", lastsave, 1},
	CommandInfo{"CONFIG", config, -2},
};
} // namespace

std::span<const CommandInfo> server_commands() { return COMMANDS; }

} // namespace redis
