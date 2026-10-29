#include "database.hpp"

#include <utility>

namespace redis {

Object *Database::find(std::string_view key) {
	if (expire_if_due(key)) return nullptr;
	const auto it = dict_.find(key);
	return it == dict_.end() ? nullptr : &it->second;
}

bool Database::contains(std::string_view key) { return find(key) != nullptr; }

void Database::set(std::string_view key, Object value) {
	if (auto it = dict_.find(key); it != dict_.end())
		it->second = std::move(value);
	else dict_.emplace(std::string(key), std::move(value));
}

bool Database::erase(std::string_view key) {
	if (auto it = expires_.find(key); it != expires_.end()) erase_deadline(it);
	const auto it = dict_.find(key);
	if (it == dict_.end()) return false;
	dict_.erase(it);
	return true;
}

void Database::erase_if_empty(std::string_view key) {
	const auto it = dict_.find(key);
	if (it == dict_.end()) return;
	const bool empty = std::visit(
		[]<typename T>(const T &value) {
			if constexpr (std::is_same_v<T, String>) return false;
			else return value.empty();
		},
		it->second);
	if (empty) erase(key);
}

void Database::clear() noexcept {
	dict_.clear();
	expires_.clear();
	by_deadline_.clear();
}

bool Database::set_expiry(std::string_view key, time_point deadline) {
	if (!contains(key)) return false;
	if (deadline <= clock::now()) {
		erase(key);
		return true;
	}
	if (auto it = expires_.find(key); it != expires_.end()) erase_deadline(it);
	auto [it, _] = expires_.emplace(std::string(key), deadline);
	by_deadline_.emplace(deadline, it->first);
	return true;
}

bool Database::persist(std::string_view key) {
	const auto it = expires_.find(key);
	if (it == expires_.end()) return false;
	erase_deadline(it);
	return true;
}

std::optional<Database::time_point>
Database::expiry(std::string_view key) const {
	const auto it = expires_.find(key);
	if (it == expires_.end()) return std::nullopt;
	return it->second;
}

size_t Database::active_expire_cycle(size_t max_keys) {
	const auto now = clock::now();
	size_t removed = 0;
	while (removed < max_keys && !by_deadline_.empty() &&
		   by_deadline_.begin()->first <= now) {
		// Copy: erase() destroys the set node that owns the key string.
		const std::string key = by_deadline_.begin()->second;
		erase(key);
		++removed;
	}
	return removed;
}

bool Database::expire_if_due(std::string_view key) {
	const auto it = expires_.find(key);
	if (it == expires_.end() || it->second > clock::now()) return false;
	erase(key);
	return true;
}

void Database::erase_deadline(KeyMap<time_point>::iterator it) {
	by_deadline_.erase({it->second, it->first});
	expires_.erase(it);
}

} // namespace redis
