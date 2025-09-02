#pragma once

#include "core/logging.hpp"
#include "core/utils/deadline_timer.hpp"
#include "core/utils/error_enum_formatter.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

namespace redis {
class Database {
public:
	using key_type   = std::string;
	using value_type = std::string;

	using dict_type   = std::unordered_map<key_type, value_type>;
	using key_pointer = dict_type::iterator;

	Database(int id);

	bool contains_key(const key_type &key);

	size_t erase_key(const key_type &key);
	void save() const;


	void set_value(const key_type &key, const value_type &value);
	[[nodiscard]] value_type &get_value_by_key(const key_type &key);
	[[nodiscard]] const value_type &get_value_by_key(const key_type &key) const;

	void set_expire(const key_type &key, const DeadlineTimer &deadline);

	[[nodiscard]] int get_id() const;


	/**
	 * \brief Get TTL in milliseconds (like Redis PTTL command)
	 */
	[[nodiscard]] int64_t pttl(const std::string &key) const;


	/**
	 * \brief Checks whether a given key has expired and removes it if so.
	 *
	 * This function looks up the key in the expiration dictionary.
	 * - If the key has no associated expiration time, it is considered
	 * persistent and not expired.
	 * - If the key has an expiration time and the current snapshot time is
	 * greater than or equal to that expiration time, the key is considered
	 * expired.
	 *
	 * In the case of expiration:
	 * - The key is removed from both the main dictionary and the expiration
	 * dictionary
	 * - A trace message is logged to indicate the expiration event.
	 *
	 * \param key The key to check for expiration.
	 * \return true  If the key exists and has expired (the key will also be
	 * removed).
	 * \return false If the key does not exist in the expiration dictionary, or
	 * if it has not yet expired.
	 */
	void erase_expired_key(const std::string &key);

	enum class ExpireError { NO_SUCH_KEY, NO_ASSOCIATE_EXPIRY };
	/**
	 * \brief Get TTL in seconds (like Redis TTL command)
	 * Returns: -2 if key does not exist, -1 if exists but no expire, otherwise
	 * seconds to expire
	 */
	[[nodiscard]] std::expected<DeadlineTimer, ExpireError>
	deadline_timer_of(const std::string &key);

private:
	dict_type dict_;
	std::unordered_map<key_type, DeadlineTimer> expires_;
	int id_;
	int64_t avg_ttl = 0;


	CLASS_LOGGER(Database);
};

} // namespace redis

std::string_view as_error_string(redis::Database::ExpireError c);

ENUM_DISPLAY_DEBUG_FORMATTER(redis::Database::ExpireError);
