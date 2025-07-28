#pragma once

#include "error.hpp"
#include "primitive.hpp"

#include <chrono>
#include <concepts>
#include <list>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * \brief Specifies where to insert or remove list elements.
 */
enum class Where { LBACK = 1, LFRONT = 0 };

/**
 * \brief Represents the Redis-like in-memory database.
 */
class Db {
public:
	using key_type   = std::unordered_map<std::string, Key>;
	using value_type = std::list<Value>;

	/** \brief Returns a list of elements in a list by range. */
	ErrorOr<std::vector<std::string>>
	cmdLrange(const std::vector<std::string> &args);

	/** \brief Adds elements to a set. */
	ErrorOr<size_t> cmdSadd(const std::vector<std::string> &args);

	/** \brief Removes elements from a set. */
	ErrorOr<size_t> cmdSrem(const std::vector<std::string> &args);

	/** \brief Gets the number of elements in a set. */
	ErrorOr<size_t> cmdScard(const std::vector<std::string> &args);

	/** \brief Gets the intersection of multiple sets. */
	ErrorOr<std::vector<std::string>>
	cmdSinter(const std::vector<std::string> &keys);

	/** \brief Gets all the members of a set. */
	ErrorOr<std::vector<std::string>>
	cmdSmembers(const std::vector<std::string> &args);

	/** \brief Gets the value of a string key. */
	ErrorOr<std::string> cmdGet(const std::vector<std::string> &args);

	/** \brief Sets a string value. */
	ErrorOr<void> cmdSet(const std::vector<std::string> &args);

	/** \brief Gets all keys matching a pattern (wildcard support TBD). */
	ErrorOr<std::vector<std::string>>
	cmdKeys(const std::vector<std::string> &args);

	/** \brief Pushes a value to a list (front or back). */
	ErrorOr<size_t> cmdPush(const std::vector<std::string> &args, Where where);

	/** \brief Gets time-to-live (TTL) in seconds for a key. */
	ErrorOr<std::chrono::seconds> cmdTTL(const std::vector<std::string> &args);

	/** \brief Clears the entire database. */
	ErrorOr<void> cmdFlush(const std::vector<std::string> &args);

	/** \brief Sets the expiration time for a key. */
	ErrorOr<std::chrono::seconds>
	cmdExpire(const std::vector<std::string> &args);

	/** \brief Deletes one or more keys. */
	ErrorOr<bool> cmdDel(const std::vector<std::string> &args);

	/** \brief Gets the length of a list. */
	ErrorOr<size_t> cmdLlen(const std::vector<std::string> &args);

	/** \brief Pops an element from a list (front or back). */
	ErrorOr<std::string> cmdPop(const std::vector<std::string> &args,
								Where where);

protected:
	/**
	 * \brief Checks if a key has expired.
	 * \param key_iter Iterator to the key entry in the key map.
	 * \return True if expired, false otherwise.
	 */
	[[nodiscard]] bool isExpired(key_type::iterator key_iter) const;

	/**
	 * \brief Gets the value iterator associated with a key, if it exists.
	 */
	[[nodiscard]] std::optional<value_type::iterator>
	getValIterFromKey(const std::string &key) const;

	/**
	 * \brief Removes a value from the value list.
	 */
	void deleteVal(value_type::iterator val_iter);

	/**
	 * \brief Inserts or updates a key-value pair.
	 */
	template <isRedisValue T>
	std::pair<key_type::iterator, value_type::iterator>
	writeKV(const std::string &key, const T &val) {
		// TODO
		auto it = getValIterFromKey(key);
		if (it.has_value()) deleteVal(*it);
		Key new_key{.key      = key,
					.val_iter = values_.insert(values_.end(), Value{val})};
		keys_[key]        = new_key;
		last_access_[key] = std::chrono::system_clock::now();
		return {keys_.find(key), new_key.val_iter};
	}

	/**
	 * \brief Modifies the value at a key using a custom modifier function.
	 */
	std::optional<value_type::iterator>
	modifyVal(key_type::iterator key_iter,
			  std::invocable<Value &> auto modifier) {
		if (key_iter == keys_.end()) return std::nullopt;
		modifier(*key_iter->second.val_iter);
		return key_iter->second.val_iter;
	}

	/**
	 * \brief Deletes a key-value pair.
	 */
	bool deleteKV(const std::string &key);

	/**
	 * \brief Performs pre-processing checks like TTL validation.
	 */
	void preCommand(const std::vector<std::string> &keys,
					bool all_keys = false);
	void preCommand(const std::string &key_to_check, bool all_keys = false);

	/**
	 * \brief Gets TTL as a raw number of seconds.
	 */
	ErrorOr<long long> getTTL(const std::string &key);

	/**
	 * \brief Pushes multiple values into a list.
	 */
	size_t pushList(const std::string &key,
					const std::vector<std::string> &vals, Where where);

	/**
	 * \brief Post-processing access hook.
	 */
	void postAccessCommand(const std::string &key, bool all_keys = false);
	void postAccessCommand(const std::vector<std::string> &keys,
						   bool all_keys = false);

	/**
	 * \brief Gets the length of a list if key exists and is a list.
	 */
	std::optional<size_t> getListLen(const std::string &key);

	/**
	 * \brief Pops a value from a list.
	 */
	std::optional<std::string> popList(const std::string &key, Where where);

	/**
	 * \brief Inserts a batch of values into a set.
	 */
	size_t insertSet(const std::string &key, std::span<const std::string> vals);

	/**
	 * \brief Removes a batch of values from a set.
	 */
	size_t removeSet(const std::string &key, std::span<const std::string> vals);

	/**
	 * \brief Gets the length of a set.
	 */
	std::optional<size_t> getSetLen(const std::string &key);

	/**
	 * \brief Gets all members of a set.
	 */
	std::optional<std::vector<std::string>> getSetMems(const std::string &key);

	/**
	 * \brief Gets intersection of multiple sets.
	 */
	std::vector<std::string> getSetInter(const std::vector<std::string> &keys);

	/**
	 * \brief Gets string value at a key.
	 */
	std::optional<std::string> getStr(const std::string &key);

	/**
	 * \brief Sets a string value at a key.
	 */
	void setStr(const std::string &key, const std::string &val);

	/**
	 * \brief Sets time-to-live for a key.
	 */
	ErrorOr<std::chrono::seconds> setTTL(const std::string &key,
										 std::chrono::seconds ttl);

private:
	key_type keys_;
	value_type values_;
	std::unordered_map<std::string, std::chrono::system_clock::time_point>
		last_access_;
	std::shared_timed_mutex vals_mtx_, keys_mtx_, last_access_mtx_;
};
