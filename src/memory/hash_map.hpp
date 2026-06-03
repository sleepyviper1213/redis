#include "core/error.hpp"

#include <expected>
#include <optional>
#include <unordered_map>
#include <utility>

namespace redis {
/*
struct Hash {
	auto operator()(const std::string &s) const noexcept {
		return XXH3_64bits(s.data(), s.size());
	}
};
*/
template <typename K, typename V>
	requires std::equality_comparable<K> && std::movable<V>
class HashMap {
public:
	using Map         = std::unordered_map<K, V>;
	using key_type    = typename Map::key_type;
	using mapped_type = typename Map::mapped_type;
	using value_type =
		typename Map::value_type; // Typically std::pair<const K, V>
	using reference       = std::reference_wrapper<value_type>;
	using const_reference = std::reference_wrapper<const value_type>;
	using iterator        = typename Map::iterator;
	using const_iterator  = typename Map::const_iterator;

	enum class Error { KEY_NOT_FOUND, INVALID_OPERATION };

	// Return reference if key exists, otherwise std::nullopt
	OptionalRef<V> get(const K &key) {
		auto it = map_.find(key);
		if (it == map_.end()) return std::nullopt;
		return std::ref(it->second);
	}

	OptionalRef<const V> get(const K &key) const {
		auto it = map_.find(key);
		if (it == map_.end()) return std::nullopt;
		return std::cref(it->second);
	}

	OptionalRef<V> set(const K &key, V value) {
		auto [it, inserted] = map_.insert_or_assign(key, std::move(value));
		if (inserted) return std::nullopt;
		return it->second; // reference to old value
	}

	template <std::ranges::input_range Range>
	ErrorRefOr<V> insert_or_append_range(const K &key, Range &&range) {
		auto it = map_.find(key);
		if (it != map_.end())
			return it->second.insert_range(std::forward<Range>(range));
		// Create new container initialized from range
		auto [new_it, _] =
			map_.try_emplace(key, std::forward_like<Range>(range));
		return std::ref(new_it->second);
	}

	struct OccupiedEntry {
		typename Map::iterator it;
		Map *map;

		V &value() { return it->second; }

		const V &value() const { return it->second; }

		const K &key() const { return it->first; }
	};

	using TryInsertResult = redis::ErrorRefOr<V, OccupiedEntry>;

	TryInsertResult try_insert(const K &key, V value) {
		auto [it, inserted] = map_.emplace(key, std::move(value));
		if (!inserted)               // failure: occupied
			return std::unexpected(std::in_place, it, &map_);
		return std::ref(it->second); // success: reference to new value
	}

	OptionalRef<V> insert(const key_type &key, const mapped_type &value) {
		auto [iter, inserted] = map_.insert_or_assign(key, value);
		if (!inserted) return std::nullopt;
		return std::ref(iter->second);
	}

	// Try to remove
	bool erase(const K &key) { return map_.erase(key) > 0; }

	bool contains(const K &key) const { return map_.find(key) != map_.end(); }

	[[nodiscard]] size_t size() const { return map_.size(); }

	[[nodiscard]] iterator begin() { return map_.begin(); }

	[[nodiscard]] const_iterator begin() const { return map_.begin(); }

	[[nodiscard]] iterator end() { return map_.end(); }

	[[nodiscard]] const_iterator end() const { return map_.end(); }

	[[nodiscard]] iterator find(const K &key) { return map_.find(key); }


private:
	Map map_;
};
} // namespace redis
