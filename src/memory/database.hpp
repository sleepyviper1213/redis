#pragma once

#include "resp/value.hpp"
#include "utils/deadline_timer.hpp"
#include "core.hpp"
#include "hash_map.hpp"
#include "resp.hpp"

#include <expected>
#include <string>
#include <string_view>

namespace redis {
class Database {
public:
	using key_type   = std::string;
	using value_type = resp::Value;

	explicit Database(int id);

	bool contains_key(const key_type &key);

	size_t erase_key(const key_type &key);
	void save() const;


	void set_value(const key_type &key, const value_type &value);
	[[nodiscard]] OptionalRef<value_type> get_value_by_key(const key_type &key);
	[[nodiscard]] OptionalRef<const value_type>
	get_value_by_key(const key_type &key) const;

	void set_expire(const key_type &key, const DeadlineTimer &deadline);

	[[nodiscard]] int get_id() const;

	template <std::ranges::input_range Range>
	ErrorRefOr<resp::Value> insert_or_append_range(const key_type &key,
												   Range &&range) {
		auto result =
			dict_.insert_or_append_range(key, std::forward<Range>(range));

		if (!result) {
			// Handle the generic HashMap error in RESP terms
			BAIL(RedisError::Kind::WRONG_TYPE);
		}
		return result;
	}

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
	HashMap<key_type, value_type> dict_;

	HashMap<key_type, DeadlineTimer> expires_;
	int id_;
	int64_t avg_ttl_ = 0;


	CLASS_LOGGER(Database);
};

} // namespace redis

std::string_view as_error_string(redis::Database::ExpireError c);

ENUM_DISPLAY_DEBUG_FORMATTER(redis::Database::ExpireError);
