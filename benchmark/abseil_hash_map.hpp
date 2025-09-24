#pragma once
#include "interface.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h> // For default string hasher

template <typename K, typename V, typename Hash>
class AbseilHashMap : public OptimizedHashMap<K, V, Hash> {
private:
	absl::flat_hash_map<K, V, Hash> map_;

public:
	bool insert(const K &key, const V &value) override {
		return map_.insert_or_assign(key, value).second;
	}

	V *find(const K &key) override {
		auto it = map_.find(key);
		return it != map_.end() ? &it->second : nullptr;
	}

	void reserve(size_t size) override { map_.reserve(size); }
};
