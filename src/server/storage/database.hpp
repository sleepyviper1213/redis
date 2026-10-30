#pragma once
#include "core/utils/attributes.hpp"
#include "server/storage/database_fwd.hpp"
#include "server/storage/object.hpp"

#include <chrono>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <type_traits>
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
 *
 * \tparam A A standard Allocator. Keys, values, the tables and the deadline
 *         index all allocate from the one the database is constructed with.
 */
template <typename Allocator>
class BasicDatabase {
	using Types = BasicValueTypes<Allocator>;

	template <typename V>
	using KeyMap = typename Types::template KeyMap<V>;

	template <typename T>
	using rebound = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

public:
	using allocator_type = Allocator;
	using clock          = std::chrono::system_clock;
	using time_point     = clock::time_point;
	using String         = typename Types::String;
	using Key            = typename Types::Key;
	using Object         = BasicObject<Allocator>;

	BasicDatabase() requires std::is_default_constructible_v<Allocator>
		: BasicDatabase(Allocator()) {}

	explicit BasicDatabase(const Allocator &alloc)
		: alloc_(alloc),
		  dict_(typename KeyMap<Object>::allocator_type(alloc)),
		  expires_(typename KeyMap<time_point>::allocator_type(alloc)),
		  by_deadline_(typename DeadlineIndex::allocator_type(alloc)) {}

	[[nodiscard]] allocator_type get_allocator() const noexcept {
		return alloc_;
	}

	/**
	 * \brief Look up \p key, removing it first if it has expired.
	 * \return The value, or nullptr if the key does not exist.
	 */
	[[nodiscard]] Object *find(std::string_view key) {
		if (expire_if_due(key)) return nullptr;
		const auto it = dict_.find(key);
		return it == dict_.end() ? nullptr : &it->second;
	}

	/**
	 * \brief Look up \p key expecting a value of type \p T.
	 * \return nullptr if the key does not exist; WrongType if it holds
	 *         another type.
	 */
	template <typename T>
	[[nodiscard]] std::expected<T *, WrongType> find_as(std::string_view key) {
		Object *object = find(key);
		if (object == nullptr) return nullptr;
		if (T *value = object_as<T>(*object)) return value;
		return std::unexpected(WrongType{});
	}

	/**
	 * \brief Look up \p key expecting a string. Strings are read and written
	 * through the object (string(), assign_string()), since a short one is
	 * embedded in it rather than held in a String.
	 * \return nullptr if the key does not exist; WrongType if it holds
	 *         another type.
	 */
	[[nodiscard]] std::expected<Object *, WrongType>
	find_string(std::string_view key) {
		Object *object = find(key);
		if (object == nullptr || object->type() == ObjectType::String)
			return object;
		return std::unexpected(WrongType{});
	}

	/**
	 * \brief Like find_string(), but stores an empty string when the key is
	 * missing. Used by APPEND.
	 */
	[[nodiscard]] std::expected<Object *, WrongType>
	find_or_create_string(std::string_view key) {
		auto found = find_string(key);
		if (!found || *found != nullptr) return found;
		auto [it, _] =
			dict_.emplace(make_key(key), Object::make_string(alloc_, {}));
		return &it->second;
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
		auto [it, _] =
			dict_.emplace(make_key(key), Object::template make<T>(alloc_));
		return object_as<T>(it->second);
	}

	/**
	 * \brief Store \p value at \p key, replacing any previous value. An
	 * existing deadline is kept; callers decide whether to clear it.
	 */
	void set(std::string_view key, Object value) {
		if (auto it = dict_.find(key); it != dict_.end())
			it->second = std::move(value);
		else dict_.emplace(make_key(key), std::move(value));
	}

	/**
	 * \brief Remove \p key and its deadline.
	 * \return true if the key existed.
	 */
	bool erase(std::string_view key) {
		if (!expires_.empty())
			if (auto it = expires_.find(key); it != expires_.end())
				erase_deadline(it);
		const auto it = dict_.find(key);
		if (it == dict_.end()) return false;
		dict_.erase(it);
		return true;
	}

	/**
	 * \brief Remove \p key if it holds an empty collection. Redis never keeps
	 * empty lists, sets or hashes; commands that remove elements call this.
	 */
	void erase_if_empty(std::string_view key) {
		const auto it = dict_.find(key);
		if (it == dict_.end()) return;
		const bool empty = it->second.visit([]<typename T>(const T &value) {
			if constexpr (std::is_same_v<T, std::string_view>) return false;
			else return value.empty();
		});
		if (empty) erase(key);
	}

	[[nodiscard]] bool contains(std::string_view key) {
		return find(key) != nullptr;
	}

	/// Number of keys, including expired ones not yet removed.
	[[nodiscard]] size_t size() const noexcept { return dict_.size(); }

	void clear() noexcept {
		dict_.clear();
		expires_.clear();
		by_deadline_.clear();
	}

	/**
	 * \brief Expire \p key at \p deadline. A deadline in the past deletes the
	 * key immediately, as EXPIRE with a non-positive TTL does in Redis.
	 * \return false if the key does not exist.
	 */
	bool set_expiry(std::string_view key, time_point deadline) {
		if (!contains(key)) return false;
		if (deadline <= clock::now()) {
			erase(key);
			return true;
		}
		if (auto it = expires_.find(key); it != expires_.end())
			erase_deadline(it);
		auto [it, _] = expires_.emplace(make_key(key), deadline);
		by_deadline_.emplace(deadline, it->first);
		return true;
	}

	/**
	 * \brief Remove the deadline of \p key, making it persistent.
	 * \return true if a deadline was removed.
	 */
	bool persist(std::string_view key) {
		// Most keyspaces have few or no deadlines: skip the hash of the key.
		if (expires_.empty()) return false;
		const auto it = expires_.find(key);
		if (it == expires_.end()) return false;
		erase_deadline(it);
		return true;
	}

	/**
	 * \brief The deadline of \p key, or std::nullopt if it has none. The key
	 * must exist; check with contains() or find() first.
	 */
	[[nodiscard]] std::optional<time_point>
	expiry(std::string_view key) const {
		const auto it = expires_.find(key);
		if (it == expires_.end()) return std::nullopt;
		return it->second;
	}

	/**
	 * \brief Remove up to \p max_keys keys whose deadline has passed, oldest
	 * first. Bounded so one cycle never stalls the event loop.
	 * \return Number of keys removed.
	 */
	size_t active_expire_cycle(size_t max_keys) {
		const auto now = clock::now();
		size_t removed = 0;
		while (removed < max_keys && !by_deadline_.empty() &&
			   by_deadline_.begin()->first <= now) {
			// Copy: erase() destroys the set node that owns the key.
			const Key key = by_deadline_.begin()->second;
			erase(key);
			++removed;
		}
		return removed;
	}

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
	using DeadlineIndex =
		std::set<std::pair<time_point, Key>, std::less<>,
				 rebound<std::pair<time_point, Key>>>;

	[[nodiscard]] Key make_key(std::string_view key) const {
		return Key(key, rebound<char>(alloc_));
	}

	/// Remove \p key if its deadline has passed. \return true if removed.
	bool expire_if_due(std::string_view key) {
		if (expires_.empty()) return false;
		const auto it = expires_.find(key);
		if (it == expires_.end() || it->second > clock::now()) return false;
		erase(key);
		return true;
	}

	void erase_deadline(typename KeyMap<time_point>::iterator it) {
		by_deadline_.erase({it->second, it->first});
		expires_.erase(it);
	}

	REDIS_NO_UNIQUE_ADDRESS Allocator alloc_;
	KeyMap<Object> dict_;
	KeyMap<time_point> expires_;
	DeadlineIndex by_deadline_;
};

} // namespace redis
