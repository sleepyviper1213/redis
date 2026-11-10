// Connection and server commands: PING, ECHO, SAVE, BGSAVE, LASTSAVE,
// CONFIG GET, CONFIG SET loglevel.
#include "command.hpp"
#include "common/store/allocator.hpp"
#include "core/chrono/date_time.hpp"
#include "server/log_channels.hpp"
#include "core/utils/ascii.hpp"
#include "core/utils/glob.hpp"
#include "common/store/database.hpp" // IWYU pragma: keep
#include "server/net/guard.hpp"
#include "server/persistence/append_only_file.hpp"
#include "server/persistence/persist_error.hpp"
#include "server/persistence/snapshot_manager.hpp"
#include "server/stats.hpp"

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>
#include <spdlog/logger.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace redis {
using resp::Value;

namespace {
Value ping(CommandContext & /*unused*/, Args args) {
	if (args.size() > 2) {
		return reply::error("ERR wrong number of arguments for 'ping' command");
	}
	if (args.size() == 1) {
		return reply::simple("PONG");
	}
	return reply::bulk(args[1]);
}

Value echo(CommandContext & /*unused*/, Args args) {
	return reply::bulk(args[1]);
}

// The error reply for a persistence failure: the protocol's "ERR", then the
// sentence. The wording belongs to PersistError; only the prefix is the command
// layer's.
Value reply_failure(const persistence::PersistError &error) {
	return reply::error("ERR {}", error);
}

Value save(CommandContext &ctx, Args /*unused*/) {
	const auto result = ctx.persistence.save(ctx.db);
	return result ? reply::ok() : reply_failure(result.error());
}

Value bgsave(CommandContext &ctx, Args /*unused*/) {
	const auto result = ctx.persistence.background_save(ctx.db);
	return result ? reply::simple("Background saving started")
				  : reply_failure(result.error());
}

Value lastsave(CommandContext &ctx, Args /*unused*/) {
	const auto unix_seconds =
		ctx.persistence.last_snapshot().to_secs_since_epoch();
	return reply::integer(unix_seconds);
}

Value bgrewriteaof(CommandContext &ctx, Args /*unused*/) {
	if (ctx.aof == nullptr) {
		return reply::error("ERR Append only file is not enabled");
	}
	const auto result = ctx.aof->start_rewrite(ctx.db);
	return result ? reply::simple("Background append only file rewriting started")
				  : reply_failure(result.error());
}

// CONFIG SET loglevel <level>: the one setting worth changing without a
// restart, to look closer at a running server without losing its state.
Value config_set(const CommandContext &ctx, Args args) {
	if (!ctx.config_set_enabled) {
		return reply::error("ERR CONFIG SET is disabled; start the server with "
				  "--enable-config-set to allow it");
	}
	if (!iequals(args[2], "loglevel")) {
		return reply::error("ERR Unsupported CONFIG parameter: {}", args[2]);
	}
	if (!logging::set_level(args[3])) {
		return reply::error("ERR Invalid argument '{}' for CONFIG SET 'loglevel'",
						 args[3]);
	}
	// Warn, so the change is on record at any level short of error.
	logging::logger_for(logging::channel::server)
		.warn("Log level changed to {} by CONFIG SET", logging::level());
	return reply::ok();
}

// CONFIG GET <pattern>: read-only view of the settings clients commonly probe
// (redis-benchmark asks for "save" and "appendonly" on startup).
Value config(CommandContext &ctx, Args args) {
	if (iequals(args[1], "SET") && args.size() == 4) {
		return config_set(ctx, args);
	}
	if (!iequals(args[1], "GET") || args.size() != 3) {
		return reply::error("ERR CONFIG supports only 'CONFIG GET <pattern>' and "
				  "'CONFIG SET loglevel <level>'");
	}

	// Views, except for the two path parts, which have to live somewhere.
	const std::string filename = ctx.persistence.file().filename().string();
	const std::string dir      = ctx.persistence.file().parent_path().string();
	const std::array<std::pair<std::string_view, std::string_view>, 6>
		parameters{{
			{"appendonly", ctx.aof != nullptr ? "yes" : "no"},
			{"appendfsync",
			 magic_enum::enum_name(ctx.aof != nullptr ? ctx.aof->fsync_policy()
													  : AofConfig{}.fsync)},
			{"dbfilename", filename},
			{"dir", dir},
			{"loglevel", logging::level()},
			{"save", ""},
		}};

	const std::string_view pattern = args[2];
	size_t matches                 = 0;
	for (const auto &[name, value] : parameters)
		matches += glob_match(pattern, name) ? 1 : 0;

	Value::Array found;
	found.reserve(matches * 2);
	for (const auto &[name, value] : parameters) {
		if (!glob_match(pattern, name)) continue;
		found.push_back(reply::bulk(name));
		found.push_back(reply::bulk(value));
	}
	return reply::array(std::move(found));
}

/// "1.50M" for 1572864, as Redis writes used_memory_human.
std::string human_bytes(size_t bytes) {
	constexpr std::array<char, 4> UNITS{'K', 'M', 'G', 'T'};
	if (bytes < 1024) return fmt::format("{}B", bytes);
	auto value  = static_cast<double>(bytes) / 1024;
	size_t unit = 0;
	while (value >= 1024 && unit + 1 < UNITS.size()) {
		value /= 1024;
		++unit;
	}
	return fmt::format("{:.2f}{}", value, UNITS[unit]);
}

// INFO [section ...]: "# Name" blocks of "field:value" lines, as Redis writes
// them. Sections whose source is missing (no stats, no network) are left empty
// of those fields. An unknown section yields an empty reply, as in Redis.
Value info(CommandContext &ctx, Args args) {
	// "default" (and no argument at all) leaves out the sections that are long
	// or costly, as Redis does; "all" and "everything" include them.
	const auto wants = [&](std::string_view section, bool in_default = true) {
		if (args.size() == 1) return in_default;
		return std::ranges::any_of(args.subspan(1), [&](std::string_view name) {
			if (iequals(name, section)) return true;
			if (iequals(name, "default")) return in_default;
			return iequals(name, "all") || iequals(name, "everything");
		});
	};

	std::string text;
	const auto begin = [&](std::string_view title) {
		fmt::format_to(std::back_inserter(text),
					   "{}# {}\r\n",
					   text.empty() ? "" : "\r\n",
					   title);
	};

	if (wants("server")) {
		begin("Server");
		if (ctx.stats != nullptr)
			fmt::format_to(std::back_inserter(text),
						   "uptime_in_seconds:{}\r\n",
						   ctx.stats->uptime().count());
	}
	if (wants("clients")) {
		begin("Clients");
		if (ctx.guard != nullptr)
			fmt::format_to(std::back_inserter(text),
						   "connected_clients:{}\r\nmaxclients:{}\r\n",
						   ctx.guard->clients(),
						   ctx.guard->limits().max_clients);
	}
	if (wants("memory")) {
		begin("Memory");
		const auto used = DefaultAllocator::bytes_in_use();
		fmt::format_to(std::back_inserter(text),
					   "used_memory:{}\r\nused_memory_human:{}\r\n",
					   used,
					   human_bytes(used));
	}
	if (wants("stats")) {
		begin("Stats");
		if (ctx.guard != nullptr) {
			const auto rejected = ctx.guard->count(Violation::TooManyClients);
			fmt::format_to(std::back_inserter(text),
						   "total_connections_received:{}\r\n"
						   "rejected_connections:{}\r\n",
						   ctx.guard->admitted() + rejected,
						   rejected);
		}
		if (ctx.stats != nullptr)
			fmt::format_to(std::back_inserter(text),
						   "total_commands_processed:{}\r\n"
						   "instantaneous_ops_per_sec:{}\r\n",
						   ctx.stats->commands_processed(),
						   ctx.stats->ops_per_sec());
		fmt::format_to(std::back_inserter(text),
					   "expired_keys:{}\r\n"
					   "keyspace_hits:{}\r\n"
					   "keyspace_misses:{}\r\n",
					   ctx.db.expired_keys(),
					   ctx.db.keyspace_hits(),
					   ctx.db.keyspace_misses());
	}
	if (wants("commandstats", false)) {
		begin("Commandstats");
		if (ctx.stats != nullptr)
			for (const auto &[name, stat] : ctx.stats->by_command()) {
				const double usec = static_cast<double>(stat.nanos) / 1e3;
				fmt::format_to(std::back_inserter(text),
							   "cmdstat_{}:calls={},usec={:.0f},"
							   "usec_per_call={:.2f}\r\n",
							   ascii_lowered(name),
							   stat.calls,
							   usec,
							   usec / static_cast<double>(stat.calls));
			}
	}
	if (wants("persistence")) {
		begin("Persistence");
		fmt::format_to(std::back_inserter(text),
					   "rdb_bgsave_in_progress:{}\r\n"
					   "rdb_last_save_time:{}\r\n"
					   "aof_enabled:{}\r\n",
					   ctx.persistence.background_save_in_progress() ? 1 : 0,
					   ctx.persistence.last_snapshot().to_secs_since_epoch(),
					   ctx.aof != nullptr ? 1 : 0);
		if (ctx.aof != nullptr)
			fmt::format_to(std::back_inserter(text),
						   "aof_rewrite_in_progress:{}\r\n"
						   "aof_fsync_policy:{}\r\n",
						   ctx.aof->rewrite_in_progress() ? 1 : 0,
						   magic_enum::enum_name(ctx.aof->fsync_policy()));
	}
	if (wants("keyspace")) {
		begin("Keyspace");
		if (ctx.db.size() > 0)
			fmt::format_to(std::back_inserter(text),
						   "db0:keys={},expires={}\r\n",
						   ctx.db.size(),
						   ctx.db.expiring());
	}
	return reply::bulk(text);
}

constexpr std::array COMMANDS{
	CommandInfo{.name = "INFO", .fn = info, .arity = -1},
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
