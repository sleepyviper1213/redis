#include "gate.hpp"

#include "actions/snapshot.hpp"
#include "commands/command_type.hpp"
#include "utils/visitor_helper_type.hpp"

#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

#include <string>

ErrorOr<Ret> Gate::parseAndExecute(const std::string &cmd) {
	auto command = TRY(Command::fromString(cmd));
	spdlog::info("[Gate] Received command {}", command);

	switch (command.type()) {
		using enum CommandType;
	case EXIT: return failed("stop", std::errc::operation_canceled);
	case SNAPSHOT:
		spdlog::info("[COMMAND] Save");
		if (!command.args().empty())
			return failed("[Snapshot] Save command expected no argument",
						  std::errc::invalid_argument);
		TRY(snapshot_.createFrom(db_));
		snapshot_.addCommand(command);
		return Ret{"Snapshot created"};
	case RESTORE: {
		spdlog::info("[COMMAND] Restore");
		// auto tmpDb = TRY(snapshot.restoreSnapshot());
		// db         = std::move(tmpDb);

		return Ret{"Snapshot restored"};
	}
	case KEYS: return db_.cmdKeys(command.args());
	case DEL: return db_.cmdDel(command.args());
	case FLUSHDB:
		TRY(db_.cmdFlush(command.args()));
		return Ret{"All database was flushed"};
	case TTL:
		return db_.cmdTTL(command.args()).value_or(std::chrono::seconds(0));
	case EXPIRE:
		return db_.cmdExpire(command.args()).value_or(std::chrono::seconds(0));
	case RPUSH: return db_.cmdPush(command.args(), Where::LBACK);
	case LPUSH: return db_.cmdPush(command.args(), Where::LFRONT);
	case LLEN: return db_.cmdLlen(command.args());
	case LPOP: return db_.cmdPop(command.args(), Where::LFRONT);
	case RPOP: return db_.cmdPop(command.args(), Where::LBACK);
	case LRANGE: return db_.cmdLrange(command.args());
	case SADD: return db_.cmdSadd(command.args());
	case SREM: return db_.cmdSrem(command.args());
	case SCARD: return db_.cmdScard(command.args());
	case SMEMBERS: return db_.cmdSmembers(command.args());
	case SINTER: return db_.cmdSinter(command.args());
	case SGET: return db_.cmdGet(command.args());
	case SSET:
		return db_.cmdSet(command.args()).transform([]() {
			return Ret{"A new string has been set"};
		});
	}
}

std::string format_as(const ErrorOr<Ret> &x) {
	if (!x.has_value()) {
		spdlog::error("{}", x.error());
		return fmt::format("ERROR: {}", x.error());
	}

	return std::visit(
		overloaded{[](bool arg) { return fmt::format("{}", arg); },
				   [](size_t arg) { return fmt::format("(integer) {}", arg); },
				   [](const std::string &arg) {
					   if (arg.empty()) return std::string("(nil)");
					   return fmt::format("\"{}\"", arg);
				   },
				   [](const std::vector<std::string> &arg) {
					   if (arg.empty()) return std::string("(empty list)");
					   std::string resp = fmt::format("\n1) \"{}\"", arg[0]);
					   for (int i = 1; i < arg.size(); ++i)
						   resp +=
							   fmt::format("\n{}) \"{}\"", i + 1, arg.at(i));
					   return resp;
				   },
				   [](const std::chrono::seconds &arg) {
					   return fmt::format("\"{}\"", arg);
				   },
				   [](auto /*arg*/) {
					   spdlog::error("Something wrong");
					   //    return "";
				   }},
		x.value());
}
