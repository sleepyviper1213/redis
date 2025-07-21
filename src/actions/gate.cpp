#include "gate.hpp"

#include "actions/snapshot.hpp"
#include "commands/command_type.hpp"
#include "utils/visitor_helper_type.hpp"

#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

#include <string>

ErrorOr<Ret> Gate::parseAndExecute(const std::string &cmd) {
	auto command = TRY(Command::fromString(cmd));

	// TODO: Pattern matching
	switch (command.type()) {
		using enum CommandType;
	case EXIT: return failed("stop", std::errc::operation_canceled);
	case SNAPSHOT:
		spdlog::info("[COMMAND] Save");
		if (!command.args().empty())
			return failed("Save command takes no argument",
						  std::errc::invalid_argument);
		snapshot = TRY(Snapshot::createFrom(*db));
		snapshot->addCommand(command);
		return Ret{"Snapshot created"};
	case RESTORE:
		spdlog::info("[COMMAND] Restore");
		auto tmpDb = TRY(snapshot->restoreSnapshot());
		db         = std::move(tmpDb);

		return Ret{"Snapshot restored"};
	case KEYS: return db->cmdKeys(command);
	case DEL: return db->cmdDel(command);
	case FLUSHDB:
		return db->cmdFlush(command).transform([]() { return Ret{"dummy"}; });
	case TTL: return db->cmdTTL(command).value_or(std::chrono::seconds(0));
	case EXPIRE:
		return db->cmdExpire(command).value_or(std::chrono::seconds(0));

	case RPUSH: return db->cmdPush(command, Where::LBACK);
	case LPUSH: return db->cmdPush(command, Where::LFRONT);
	case LLEN: return db->cmdLlen(args.front()).value_or(0);
	case LPOP: return db->cmdPop(command, Where::LFRONT).value_or("Failed");
	case RPOP:
		if (args.size() != 1) return failed("", std::errc::invalid_argument);

		return db->cmdPop(args[0], Where::LBACK).value_or("Failed");
	case LRANGE:
		if (args.size() != 3) return failed("", std::errc::invalid_argument);

		return db->cmdLrange(args[0], std::stoi(args[1]), std::stoi(args[2]));

	case SADD:
		if (args.size() < 2) return failed("", std::errc::invalid_argument);

		return db->cmdSadd(args[0], {args.begin() + 1, args.end()});
		break;
	case SREM:
		if (args.size() < 2) return failed("", std::errc::invalid_argument);

		return db->cmdSrem(args[0], {args.begin() + 1, args.end()});
	case SCARD:
		if (args.size() != 1) return failed("", std::errc::invalid_argument);

		return db->cmdScard(args[0]).value_or(0);
	case SMEMBERS:
		if (args.size() != 1) return failed("", std::errc::invalid_argument);
		return db->cmdSmembers(args[0]);
	case SINTER:
		if (args.size() < 2) return failed("", std::errc::invalid_argument);
		return db->cmdSinter({args.begin(), args.end()});
	case SGET: {
		if (args.size() != 1) return failed("", std::errc::invalid_argument);
		return db->cmdGet(args[0]).value_or("Failed");
	}
	case SSET:
		if (args.size() != 2) return failed("", std::errc::invalid_argument);
		return db->cmdSet(args[0], args[1]);
	}
}

std::string format_as(const ErrorOr<Ret> &x) {
	if (!x.has_value()) {
		spdlog::error("[Parse] {}", x.error());
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