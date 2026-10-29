#pragma once
#include "server/storage/object.hpp"

#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace redis {

/// Returned when a command expects one value type and the key holds another.
struct WrongType {};

/**
 * \brief One keyspace: keys mapped to typed values, with optional expiry.
 *
 * Expired keys are removed two ways, like Redis:
 * - lazily, when a lookup finds the key past its deadline;
 * - actively, by active_expire_cycle(), so keys nobody reads again do not
 *   hold memory forever.
 *
 * Deadlines are kept in two indexes: a hash map for O(1) TTL queries, and a
 * set ordered by deadline so the active cycle removes due keys in O(log n)
 * each, without scanning or sampling.
 *
 * Deadlines use the wall clock because EXPIREAT, PXAT and snapshot files
 * exchange absolute Unix times.
 */
class Database {
public:
	using clock      = std::chrono::system_clock;
	using time_point = clock::time_point;

	/**
	 * \brief Look up \p key, removing it first if it has expired.
	 * \return The value, or nullptr if the key does not exist.
	 */
	[[nodiscard]] Object *find(std::string_view key);

	/**
	 * \brief Look up \p key expecting a value of type \p T.
	 * \return nullptr if the key does not exist; WrongType if it holds
	 *         another type.
	 */
	template <typename T>
	[[nodiscard]] std::expected<T *, WrongType> find_as(std::string_view key) {
		Object *object = find(key);
		if (object == nullptr) return nullptr;
		if (T *value = std::get_if<T>(object)) return value;
		return std::unexpected(WrongType{});
	}

	/**
	 * \brief Like find_as(), but creates an empty \p T when the key is
	 * missing. Used by commands that write to collections (LPUSH, SADD, ...).
	 */
	template <typename T>
	[[nodiscard]] std::expected<T *, WrongType>
	find_or_create(std::string_view key) {
		auto found = find_as<T>(key);
		if (!found || *found != nullptr) return found;
		auto [it, _] = dict_.emplace(std::string(key), T{});
		return &std::get<T>(it->second);
	}

	/**
	 * \brief Store \p value at \p key, replacing any previous value. An
	 * existing deadline is kept; callers decide whether to clear it.
	 */
	void set(std::string_view key, Object value);

	/**
	 * \brief Remove \p key and its deadline.
	 * \return true if the key existed.
	 */
	bool erase(std::string_view key);

	/**
	 * \brief Remove \p key if it holds an empty collection. Redis never keeps
	 * empty lists, sets or hashes; commands that remove elements call this.
	 */
	void erase_if_empty(std::string_view key);

	[[nodiscard]] bool contains(std::string_view key);

	/// Number of keys, including expired ones not yet removed.
	[[nodiscard]] size_t size() const noexcept { return dict_.size(); }

	void clear() noexcept;

	/**
	 * \brief Expire \p key at \p deadline. A deadline in the past deletes the
	 * key immediately, as EXPIRE with a non-positive TTL does in Redis.
	 * \return false if the key does not exist.
	 */
	bool set_expiry(std::string_view key, time_point deadline);

	/**
	 * \brief Remove the deadline of \p key, making it persistent.
	 * \return true if a deadline was removed.
	 */
	bool persist(std::string_view key);

	/**
	 * \brief The deadline of \p key, or std::nullopt if it has none. The key
	 * must exist; check with contains() or find() first.
	 */
	[[nodiscard]] std::optional<time_point>
	expiry(std::string_view key) const;

	/**
	 * \brief Remove up to \p max_keys keys whose deadline has passed, oldest
	 * first. Bounded so one cycle never stalls the event loop.
	 * \return Number of keys removed.
	 */
	size_t active_expire_cycle(size_t max_keys);

	/**
	 * \brief Call \p fn(key, value, deadline) for every live key. \p deadline
	 * is an std::optional<time_point>. Expired keys are skipped, not removed,
	 * so this works on a const Database (snapshots iterate a forked copy).
	 */
	template <typename Fn>
	void for_each(Fn &&fn) const {
		const auto now = clock::now();
		for (const auto &[key, value] : dict_) {
			std::optional<time_point> deadline;
			if (auto it = expires_.find(key); it != expires_.end()) {
				if (it->second <= now) continue;
				deadline = it->second;
			}
			fn(key, value, deadline);
		}
	}

private:
	/// Remove \p key if its deadline has passed. \return true if removed.
	bool expire_if_due(std::string_view key);

	void erase_deadline(KeyMap<time_point>::iterator it);

	KeyMap<Object> dict_;
	KeyMap<time_point> expires_;
	std::set<std::pair<time_point, std::string>> by_deadline_;
};

} // namespace redis
