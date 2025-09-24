#include "simple_message.hpp"

#include "errc_formatter.hpp"

#include <utility>
// clang-format off
#include <cstdlib>
#include <fmt/format.h>
// clang-format on

namespace redis {

RedisError::RedisError(Kind kind) : data_(kind) {}

RedisError::RedisError(std::string msg) : data_(std::move(msg)) {}

RedisError::RedisError(Kind kind,
					   std::unordered_map<std::string, std::string> meta)
	: data_(std::move(meta)), kind_for_meta_(kind) {}

[[nodiscard]] const RedisError::Kind *RedisError::kind() const noexcept {
	return std::holds_alternative<Kind>(data_) ? std::get_if<Kind>(&data_)
		   : kind_for_meta_                    ? &*kind_for_meta_
											   : nullptr;
}

[[nodiscard]] std::string_view RedisError::message() const {
	if (std::holds_alternative<std::string>(data_))
		return std::get<std::string>(data_);
	return {};
}

[[nodiscard]] const std::unordered_map<std::string, std::string> *
RedisError::metadata() const {
	if (std::holds_alternative<std::unordered_map<std::string, std::string>>(
			data_))
		return std::get_if<std::unordered_map<std::string, std::string>>(
			&data_);
	return nullptr;
}

std::string format_as(const RedisError &error) {
	using enum RedisError::Kind;
	if (auto msg = error.message(); !msg.empty()) return std::string(msg);

	if (const auto *k = error.kind()) {
		switch (*k) {
		case UNKNOWN_COMMAND:
			if (const auto *meta = error.metadata()) {
				return fmt::format("ERR unknown command '{}'",
								   meta->at("command"));
			}
			return "ERR unknown command";
		case WRONG_TYPE:
			return "WRONGTYPE Operation against a key holding the wrong "
				   "kind of value";
		case OOM:
			return "OOM command not allowed when used memory > 'maxmemory'";
		case MISCONF:
			return "MISCONF Redis is configured to save RDB snapshots, but "
				   "it is currently disabled";
		case READ_ONLY:
			return "READONLY You can't write against a read only replica";
		case LOADING: return "LOADING Redis is loading the dataset in memory";
		case NO_AUTH: return "NOAUTH Authentication required";
		case WRONG_PASS: return "WRONGPASS invalid username-password pair";
		case CLUSTER_DOWN: return "CLUSTERDOWN The cluster is down";
		case MASTER_DOWN: return "MASTERDOWN Link with MASTER is down";
		case AOF_WRITE_ERROR: return "AOF write error";
		case MOVED:
			if (const auto *meta = error.metadata()) {
				return fmt::format("MOVED {} {}",
								   meta->at("slot"),
								   meta->at("hostport"));
			}
			return "MOVED <slot> <host:port>";
		case ASK:
			if (const auto *meta = error.metadata()) {
				return fmt::format("ASK {} {}",
								   meta->at("slot"),
								   meta->at("hostport"));
			}
			return "ASK <slot> <host:port>";
		case TIMEOUT: return "ERR timeout";
		}
	}

	std::unreachable();
}
} // namespace redis
