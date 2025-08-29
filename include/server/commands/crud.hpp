#pragma once
#include "core/resp/value.hpp"
#include "core/utils/deadline_timer.hpp"
#include "core/utils/error_enum_formatter.hpp"

#include <cstdlib>

namespace redis {
class Database;

enum class SetFlag : uint16_t {
	NONE    = 0 << 0,
	NX      = 1 << 0, /* Set if key not exists. */
	XX      = 1 << 1, /* Set if key exists. */
	EX      = 1 << 2, /* Set if time in seconds is given */
	PX      = 1 << 3, /* Set if time in ms in given */
	KEEPTTL = 1 << 4, /* Set and keep the ttl */
	SET_GET = 1 << 5, /* Set if want to get key before set */
	EXAT    = 1 << 6, /* Set if timestamp in second is given */
	PXAT    = 1 << 7, /* Set if timestamp in ms is given */
	PERSIST = 1 << 8,
};

/*! \enum ParseSetError
 *
 *  A list specifying general categories of I/O error.
 */
enum class ParseSetError {
	NOT_AN_INTEGER_OR_OUT_OF_RANGE,
	INVAlID_EXPIRE_TIME,
	INVALID_SYNTAX
};
enum class SetKeyFlag {
	KEEPTTL       = 1,
	NO_SIGNAL     = 2,
	ALREADY_EXIST = 4,
	DOESNT_EXIST  = 8,
	ADD_OR_UPDATE = 16
};

/**
 * \brief Checks if the given SetFlag contains only one flag based on some
 * constraint.
 */
bool has_single_flag(SetFlag value, SetFlag mask);

bool has_single_key_exist_flag(SetFlag value);
bool has_single_expiration_flag(SetFlag value);
std::expected<std::pair<SetFlag, DeadlineTimer>, ParseSetError>
parseSetOptions(const resp::Value::Array &argv);

resp::Value handle_set(Database &db, const resp::Value::Array &argv);

resp::Value handle_del(Database &db, const resp::Value::Array &v);

resp::Value handle_get(redis::Database &db, const resp::Value::Array &args);
} // namespace redis

std::string_view as_error_string(redis::ParseSetError c);
ENUM_DISPLAY_DEBUG_FORMATTER(redis::ParseSetError);
