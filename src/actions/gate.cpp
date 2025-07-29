#include "gate.hpp"

#include "command.hpp"
#include "primitive/reply.hpp"

#include <chrono>
#include <string>
#include <system_error>
#include <vector>

namespace redis {

ErrorOr<Reply> Gate::parseAndExecute(const std::string &cmd) {
	auto command = TRY(Command::fromString(cmd));
	snapshot_.addCommand(command);

	switch (command.type()) {
		using enum CommandType;
	case EXIT: return failed("stop", std::errc::operation_canceled);
	case SNAPSHOT:
		if (!command.args().empty())
			return failed("[Snapshot] Save command expected no argument",
						  std::errc::invalid_argument);
		TRY(snapshot_.createFrom(db_));
		return Reply{"Snapshot created"};
	case RESTORE: {
		// auto tmpDb = TRY(snapshot.restoreSnapshot());
		// db         = std::move(tmpDb);

		return Reply{"Snapshot restored"};
	}
	case KEYS: return db_.cmdKeys(command.args());
	case DEL: return db_.cmdDel(command.args());
	case FLUSHDB:
		TRY(db_.cmdFlush(command.args()));
		return Reply{"All database was flushed"};
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
			return Reply{"A new string has been set"};
		});
	}
}
} // namespace redis
