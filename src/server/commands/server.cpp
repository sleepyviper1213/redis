// Connection and server commands: PING, ECHO, SAVE, BGSAVE, LASTSAVE,
// CONFIG GET, CONFIG SET loglevel.
#include "command.hpp"
#include "core/date_time.hpp"
#include "core/logging.hpp"
#include "core/utils/glob.hpp"
#include "core/utils/toupper.hpp"
#include "server/commands/reply.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/persistence/snapshot_manager.hpp"
#include "server/database.hpp" // IWYU pragma: keep

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>
#include <spdlog/logger.h>

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
	const auto unix_seconds = ctx.persistence.last_snapshot().to_secs_since_epoch();
	out.integer(unix_seconds);
}

void bgrewriteaof(CommandContext &ctx, Args /*unused*/, ReplyWriter &out) {
	if (ctx.aof == nullptr) {
		out.error("ERR Append only file is not enabled");
		return;
	}
	const auto result = ctx.aof->start_rewrite(ctx.db);
	if (result) out.simple("Background append only file rewriting started");
	else out.error(result.error());
}

// CONFIG SET loglevel <level>: the one setting worth changing without a
// restart, to look closer at a running server without losing its state.
void config_set(Args args, ReplyWriter &out) {
	if (tolower(std::string(args[2])) != "loglevel") {
		out.error(fmt::format("ERR Unsupported CONFIG parameter: {}", args[2]));
		return;
	}
	if (!logging::set_level(args[3])) {
		out.error(fmt::format(
			"ERR Invalid argument '{}' for CONFIG SET 'loglevel'", args[3]));
		return;
	}
	// Warn, so the change is on record at any level short of error.
	logging::logger_for(logging::channel::server)
		.warn("Log level changed to {} by CONFIG SET", logging::level());
	out.ok();
}

// CONFIG GET <pattern>: read-only view of the settings clients commonly probe
// (redis-benchmark asks for "save" and "appendonly" on startup).
void config(CommandContext &ctx, Args args, ReplyWriter &out) {
	const auto sub = toupper(std::string(args[1]));
	if (sub == "SET" && args.size() == 4) {
		config_set(args, out);
		return;
	}
	if (sub != "GET" || args.size() != 3) {
		out.error("ERR CONFIG supports only 'CONFIG GET <pattern>' and "
				  "'CONFIG SET loglevel <level>'");
		return;
	}

	const std::pair<std::string, std::string> parameters[] = {
		{"appendonly", ctx.aof != nullptr ? "yes" : "no"},
		{"appendfsync",
		 std::string(magic_enum::enum_name(
			 ctx.aof != nullptr ? ctx.aof->fsync_policy()
								: AofConfig{}.fsync))},
		{"dbfilename", ctx.persistence.file().filename().string()},
		{"dir", ctx.persistence.file().parent_path().string()},
		{"loglevel", std::string(logging::level())},
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
	CommandInfo{.name = "BGREWRITEAOF", .fn = bgrewriteaof, .arity = 1},
	CommandInfo{.name = "CONFIG", .fn = config, .arity = -2},
};
} // namespace

std::span<const CommandInfo> server_commands() { return COMMANDS; }

} // namespace redis
