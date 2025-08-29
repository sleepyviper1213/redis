#include "server/commands/crud.hpp"

#include "core/error.hpp"
#include "core/utils/get_class_logger.hpp"
#include "core/utils/toupper.hpp"
#include "server/memory/database.hpp"

#include <magic_enum/magic_enum_flags.hpp>

#include <bit>
#include <cstdint>
#include <utility>

namespace redis {
using magic_enum::enum_flags_test;
using resp::Value;

bool has_single_expiration_flag(SetFlag value) {
	using enum SetFlag;
	using namespace magic_enum::bitwise_operators;
	return has_single_flag(value, KEEPTTL | PERSIST | PX | EXAT | PXAT | EX);
}

bool has_single_key_exist_flag(SetFlag value) {
	using enum SetFlag;
	using namespace magic_enum::bitwise_operators;
	return has_single_flag(value, NX | XX);
}

bool has_single_flag(SetFlag value, SetFlag mask) {
	// Keep only bits that belong to the mask
	using namespace magic_enum::bitwise_operators;
	const uint16_t selected = magic_enum::enum_integer(mask & value);
	return std::has_single_bit(selected);
}

std::expected<std::pair<SetFlag, DeadlineTimer>, ParseSetError>
parseSetOptions(const Value::Array &argv) {
	using enum SetFlag;
	using namespace magic_enum::bitwise_operators;

	SetFlag set_flag = NONE;
	DeadlineTimer deadline;

	// --- Parse options ---
	for (size_t i = 3; i < argv.size(); i++) {
		const std::string opt = toupper(argv[i].as_string());

		if (opt == "NX" && !has_single_key_exist_flag(set_flag)) {
			set_flag |= NX;
		} else if (opt == "XX" && !has_single_key_exist_flag(set_flag)) {
			set_flag |= XX;
		} else if (opt == "GET") {
			set_flag |= SET_GET;
		} else if (opt == "KEEPTTL" && !has_single_key_exist_flag(set_flag)) {
			set_flag |= KEEPTTL;
		} else if (opt == "PERSIST" && !has_single_key_exist_flag(set_flag)) {
			set_flag |= PERSIST;
		} else if (i + 1 < argv.size()) { // Options requiring an argument
			if (opt == "EX") set_flag |= EX;
			else if (opt == "PX") set_flag |= PX;
			else if (opt == "EXAT") set_flag |= EXAT;
			else if (opt == "PXAT") set_flag |= PXAT;
			if (!has_single_expiration_flag(set_flag))
				FAILED(ParseSetError::INVALID_SYNTAX);

			const auto &next = argv[++i];
			auto val         = next.try_as_integer();
			if (!val.has_value())
				FAILED(ParseSetError::NOT_AN_INTEGER_OR_OUT_OF_RANGE);
			if (*val <= 0) FAILED(ParseSetError::INVAlID_EXPIRE_TIME);

			const int64_t expire_arg = *val;
			if (enum_flags_test(set_flag, EX))
				deadline.expire_after(expire_arg);
			else if (enum_flags_test(set_flag, EX))
				deadline.expire_after(0, expire_arg);
			else if (enum_flags_test(set_flag, EX))
				deadline.expire_at_sec(expire_arg);
			else if (enum_flags_test(set_flag, EX))
				deadline.expire_at_milli(expire_arg);
		} else {
			FAILED(ParseSetError::INVALID_SYNTAX);
		}
	}

	return std::make_pair(set_flag, deadline);
}

resp::Value handle_set(Database &db, const resp::Value::Array &argv) {
	const auto logger_       = make_logger("CommandHandler");
	const std::string &key   = argv[1].as_string();
	const std::string &value = argv[2].as_string();

	const auto opts = TRY_VALUE(parseSetOptions(argv));

	const auto [set_flag, deadline] = opts;
	// --- Apply NX/XX semantics ---
	const bool exists = db.contains_key(key);
	if ((enum_flags_test(set_flag, SetFlag::NX) && exists) ||
		(enum_flags_test(set_flag, SetFlag::XX) && !exists)) {
		return Value::from_null();
	}

	TRY_VALUE(db.set_value(key, value));

	if (has_single_expiration_flag(set_flag)) db.set_expire(key, deadline);
	return Value::from_simple_string("OK");
}

Value handle_get(redis::Database &db, const Value::Array &args) {
	const auto &key = args[1].as_string();
	db.erase_expired_key(key);
	if (!db.contains_key(key)) return Value::from_null();
	return Value::from_bulk_string(db.get_value_by_key(key));
}

Value handle_del(Database &db, const Value::Array &args) {
	if (args.size() < 2)
		return Value::from_simple_error("wrong number of arguments for DEL");

	int64_t count = 0;
	for (size_t i = 1; i < args.size(); ++i) {
		const auto &key = args[i].as_string();

		auto erased = db.erase_key(key);
		count += erased;
	}
	return Value::from_integer(count);
}
} // namespace redis

std::string_view as_error_string(redis::ParseSetError c) {
	std::string_view name;
	switch (c) {
		using enum redis::ParseSetError;
	case INVALID_SYNTAX: name = "Err syntax error"; break;
	case NOT_AN_INTEGER_OR_OUT_OF_RANGE:
		name = "ERR value is not an integer or out of range";
		break;
	case INVAlID_EXPIRE_TIME:
		name = "ERR invalid expire time in 'set' command";
		break;
	default: std::unreachable();
	}
	return name;
}
