#pragma once

#include "resp/value.hpp"
#include "utils/get_class_logger.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

namespace redis {

enum class SetFlag {
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
	/* Set if we need to remove the ttl */
};

struct SetOptionResult {
	SetFlag set_flag;
	std::chrono::system_clock::time_point expire_tp;
};
enum class SetKeyFlag {
	KEEPTTL       = 1,
	NO_SIGNAL     = 2,
	ALREADY_EXIST = 4,
	DOESNT_EXIST  = 8,
	ADD_OR_UPDATE = 16
};

class Database {
public:
	using key_type   = std::string;
	using value_type = std::string;

	using dict_type   = std::unordered_map<std::string, value_type>;
	using key_pointer = dict_type::iterator;

	using clock_type     = std::chrono::system_clock;
	using timestamp_type = std::chrono::time_point<clock_type>;


	Database(int id);

	bool contains_key(const key_type &key);

	void save() const;

	[[nodiscard]] int get_id() const;

	/**
	 * \brief time snapshot per command
	 */
	[[nodiscard]] clock_type::time_point command_time_snapshot() const;

	resp::Value handle_command(const resp::Value &command);
	resp::Value handle_set(const resp::Value::Array &v);
	resp::Value handle_get(const resp::Value::Array &v);
	resp::Value handle_ping(const resp::Value::Array &v) const;
	resp::Value handle_del(const resp::Value::Array &v);
	resp::Value handle_ttl(const resp::Value::Array &v);
	resp::Value handle_pttl(const resp::Value::Array &v);
	resp::Value handle_exists(const resp::Value::Array &v);

	ErrorOr<SetOptionResult> parseSetOptions(const resp::Value::Array &argv);

	/**
	 * \brief Get TTL in seconds (like Redis TTL command)
	 * Returns: -2 if key does not exist, -1 if exists but no expire, otherwise seconds to expire
	 */
	[[nodiscard]] int64_t ttl(const std::string &key) const;

	/**
	 * \brief Get TTL in milliseconds (like Redis PTTL command)
	 */
	[[nodiscard]] int64_t pttl(const std::string &key) const;


	[[nodiscard]] bool is_key_expired(const std::string &key);

private:
	dict_type dict_;
	std::unordered_map<key_type, timestamp_type> expires_;
	int id_;
	int64_t avg_ttl = 0;
	clock_type::time_point cmd_snapshot_{};


	CLASS_LOGGER(Database);
};
} // namespace redis
