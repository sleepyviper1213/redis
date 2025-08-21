#include "database.hpp"

#include "utils/toupper.hpp"

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>
#include <magic_enum/magic_enum_flags.hpp>
#include <spdlog/logger.h>

#include <chrono>
#include <vector>

using magic_enum::enum_flags_test;
using magic_enum::enum_flags_test_any;
using resp::Value;

namespace redis {

Database::Database(int id) : id_(id) {}

Value Database::handle_command(const Value &command) {
	// Take a consistent snapshot for this command
	cmd_snapshot_ = clock_type::now();

	logger_->trace("Received: {:?}", command);

	const auto &args      = command.as_array();
	const auto &res       = args[0].as_string();
	const std::string cmd = toupper(res);

	if (cmd == "PING") return handle_ping(args);
	else if (cmd == "SET") return handle_set(args);
	else if (cmd == "GET") return handle_get(args);
	else if (cmd == "DEL") return handle_del(args);
	else if (cmd == "EXISTS") return handle_exists(args);
	else if (cmd == "TTL") return handle_ttl(args);
	else if (cmd == "PTTL") return handle_pttl(args);
	else if (cmd == "CONFIG") return Value::from_null();
	else
		return Value::from_simple_error(
			fmt::format("unknown command '{}'", res));
}

Value Database::handle_ping(const Value::Array &args) const {
	if (args.size() > 2)
		return Value::from_simple_error("wrong number of arguments for PING");
	if (args.size() == 1) return Value::from_simple_string("PONG");
	return args[1];
}

Database::clock_type::time_point Database::command_time_snapshot() const {
	return cmd_snapshot_;
}

ErrorOr<SetOptionResult> Database::parseSetOptions(const Value::Array &argv) {
	using enum SetFlag;
	using namespace magic_enum::bitwise_operators;

	SetFlag set_flag = NONE;
	clock_type::time_point expire_tp;

	// --- Parse options ---
	for (size_t i = 3; i < argv.size(); i++) {
		std::string opt = toupper(argv[i].as_string());

		if (opt == "NX" && !enum_flags_test(set_flag, XX)) {
			set_flag |= NX;
		} else if (opt == "XX" && !enum_flags_test(set_flag, NX)) {
			set_flag |= XX;
		} else if (opt == "GET") {
			set_flag |= SET_GET;
		} else if (opt == "KEEPTTL" &&
				   !enum_flags_test_any(set_flag,
										PERSIST | EX | EXAT | PX | PXAT)) {
			set_flag |= KEEPTTL;
		} else if (opt == "PERSIST" &&
				   !enum_flags_test_any(set_flag,
										KEEPTTL | EX | EXAT | PX | PXAT)) {
			set_flag |= PERSIST;
		} else if (i + 1 < argv.size()) {
			// Options requiring an argument
			const auto &next = argv[++i];
			auto val         = next.try_as_integer();
			if (!val.has_value())
				return failed("ERR invalid expire time in set",
							  std::errc::invalid_argument);
			if (*val <= 0)
				return failed("ERR value is not an integer or out of range",
							  std::errc::result_out_of_range);

			const int64_t expire_arg = *val;
			const auto now           = command_time_snapshot();

			if (opt == "EX" &&
				!enum_flags_test_any(set_flag,
									 KEEPTTL | PERSIST | PX | EXAT | PXAT)) {
				set_flag |= EX;
				expire_tp = now + std::chrono::seconds(expire_arg);
			} else if (opt == "PX" &&
					   !enum_flags_test_any(set_flag,
											KEEPTTL | PERSIST | EX | EXAT |
												PXAT)) {
				set_flag |= PX;
				expire_tp = now + std::chrono::milliseconds(expire_arg);
			} else if (opt == "EXAT" &&
					   !enum_flags_test_any(set_flag,
											KEEPTTL | PERSIST | PX | EX |
												PXAT)) {
				set_flag |= EXAT;
				expire_tp =
					clock_type::time_point{std::chrono::seconds(expire_arg)};
			} else if (opt == "PXAT" &&
					   !enum_flags_test_any(set_flag,
											KEEPTTL | PERSIST | PX | EX |
												EXAT)) {
				set_flag |= PXAT;
				clock_type::time_point{std::chrono::milliseconds(expire_arg)};
			}
		} else {
			return failed("ERR syntax error", std::errc::protocol_error);
		}
	}
	return SetOptionResult{set_flag, expire_tp};
}

Value Database::handle_set(const Value::Array &argv) {
	if (argv.size() < 3)
		return Value::from_simple_error(
			"ERR wrong number of arguments for 'set' command");


	const std::string &key   = argv[1].as_string();
	const std::string &value = argv[2].as_string();

	auto opts = parseSetOptions(argv);
	if (!opts.has_value())
		return Value::from_simple_error(
			std::string(opts.error().contextMessage()));

	// --- Apply NX/XX semantics ---
	const bool exists = contains_key(key);
	if ((enum_flags_test(opts->set_flag, SetFlag::NX) && exists) ||
		(enum_flags_test(opts->set_flag, SetFlag::XX) && !exists)) {
		return Value::from_null();
	}

	// --- Insert or update value ---
	const auto [_, y] = dict_.insert_or_assign(key, value);
	if (y) logger_->trace("Update key");

	expires_[key] = opts->expire_tp;
	logger_->trace("Key set: {}", key);
	return Value::from_simple_string("OK");
}

Value Database::handle_get(const Value::Array &args) {
	if (args.size() != 2)
		return Value::from_simple_error("wrong number of arguments for GET");

	const auto &key = args[1].as_string();
	if (!contains_key(key)) return Value::from_null();
	return Value::from_bulk_string(dict_[key]);
}

Value Database::handle_del(const Value::Array &args) {
	if (args.size() < 2)
		return Value::from_simple_error("wrong number of arguments for DEL");

	int64_t count = 0;
	for (size_t i = 1; i < args.size(); ++i) {
		const auto &key = args[i].as_string();

		auto erased = dict_.erase(key);
		if (erased > 0) {
			expires_.erase(key);
			count += erased;

			logger_->trace("Key deleted: {}", key);
		}
	}
	return Value::from_integer(count);
}

Value Database::handle_exists(const Value::Array &args) {
	if (args.size() < 2)
		return Value::from_simple_error("wrong number of arguments for EXISTS");

	int64_t count = 0;
	for (size_t i = 1; i < args.size(); ++i) {
		const auto &key = args[i].as_string();
		if (contains_key(key)) ++count;
	}
	return Value::from_integer(count);
}

Value Database::handle_ttl(const Value::Array &args) {
	if (args.size() != 2)
		return Value::from_simple_error("wrong number of arguments for TTL");
	const auto &key = args[1].as_string();
	return Value::from_integer(ttl(key));
}

Value Database::handle_pttl(const Value::Array &args) {
	if (args.size() != 2)
		return Value::from_simple_error("wrong number of arguments for PTTL");
	const auto &key = args[1].as_string();
	return Value::from_integer(pttl(key));
}

bool Database::is_key_expired(const std::string &key) {
	auto expIt = expires_.find(key);
	if (expIt == expires_.end()) return false;

	const auto now = command_time_snapshot();
	if (now >= expIt->second) {
		dict_.erase(key);
		expires_.erase(expIt);

		logger_->trace("Key expired: {}", key);
		return true;
	}
	return false;
}

bool Database::contains_key(const std::string &key) {
	return dict_.contains(key) && !is_key_expired(key);
}

long long Database::ttl(const std::string &key) const {
	if (!dict_.contains(key)) return -2;

	const auto expIt = expires_.find(key);
	if (expIt == expires_.end()) return -1;

	const auto now = command_time_snapshot();
	const auto remaining =
		std::chrono::duration_cast<std::chrono::seconds>(expIt->second - now);

	return remaining.count() < 0 ? -2 : remaining.count();
}

int64_t Database::pttl(const std::string &key) const {
	if (!dict_.contains(key)) return -2;

	auto expIt = expires_.find(key);
	if (expIt == expires_.end()) return -1;

	auto now       = command_time_snapshot();
	auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
		expIt->second - now);

	return remaining.count() < 0 ? -2 : remaining.count();
}

int Database::get_id() const { return id_; }

void Database::save() const { logger_->trace("DB saved"); }

} // namespace redis
