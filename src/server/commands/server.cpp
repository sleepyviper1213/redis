// Connection and server commands: PING, ECHO, SAVE, BGSAVE, LASTSAVE,
// CONFIG GET.
#include "command.hpp"
#include "core/utils/glob.hpp"
#include "core/utils/toupper.hpp"
#include "server/commands/reply.hpp"
#include "server/persistence/persistence.hpp"
#include "server/storage/database.hpp" // IWYU pragma: keep

#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

namespace redis {

namespace {
void ping(CommandContext & /*unused*/, Args args, ReplyWriter &out) {
	if (args.size() > 2) {
		out.error("ERR wrong number of arguments for 'ping' command");
		return;
	}
	if (args.size() == 1) {
		out.simple("PONG");
		return;
	}
	out.bulk(args[1]);
}

void echo(CommandContext & /*unused*/, Args args, ReplyWriter &out) {
	out.bulk(args[1]);
}

void save(CommandContext &ctx, Args /*unused*/, ReplyWriter &out) {
	const auto result = ctx.persistence.save(ctx.db);
	if (result) out.ok();
	else out.error(result.error());
}

void bgsave(CommandContext &ctx, Args /*unused*/, ReplyWriter &out) {
	const auto result = ctx.persistence.background_save(ctx.db);
	if (result) out.simple("Background saving started");
	else out.error(result.error());
}

void lastsave(CommandContext &ctx, Args /*unused*/, ReplyWriter &out) {
	const auto unix_seconds =
		std::chrono::duration_cast<std::chrono::seconds>(
			ctx.persistence.last_save().time_since_epoch())
			.count();
	out.integer(unix_seconds);
}

// CONFIG GET <pattern>: read-only view of the settings clients commonly probe
// (redis-benchmark asks for "save" and "appendonly" on startup).
void config(CommandContext &ctx, Args args, ReplyWriter &out) {
	if (toupper(std::string(args[1])) != "GET" || args.size() != 3) {
		out.error("ERR CONFIG supports only 'CONFIG GET <pattern>'");
		return;
	}

	const std::pair<std::string, std::string> parameters[] = {
		{"appendonly", "no"},
		{"dbfilename", ctx.persistence.file().filename().string()},
		{"dir", ctx.persistence.file().parent_path().string()},
		{"save", ""},
	};

	const std::string_view pattern = args[2];
	size_t matches                 = 0;
	for (const auto &[name, value] : parameters)
		matches += glob_match(pattern, name) ? 1 : 0;

	out.array(matches * 2);
	for (const auto &[name, value] : parameters) {
		if (!glob_match(pattern, name)) continue;
		out.bulk(name);
		out.bulk(value);
	}
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "PING", .fn = ping, .arity = -1},
	CommandInfo{.name = "ECHO", .fn = echo, .arity = 2},
	CommandInfo{.name = "SAVE", .fn = save, .arity = 1},
	CommandInfo{.name = "BGSAVE", .fn = bgsave, .arity = 1},
	CommandInfo{.name = "LASTSAVE", .fn = lastsave, .arity = 1},
	CommandInfo{.name = "CONFIG", .fn = config, .arity = -2},
};
} // namespace

std::span<const CommandInfo> server_commands() { return COMMANDS; }

} // namespace redis
