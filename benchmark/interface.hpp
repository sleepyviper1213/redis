#pragma once
#include <concepts>
#include <cstddef>

template <typename Key, typename Value, typename Hash>
	requires std::invocable<Hash, Key>
class OptimizedHashMap {
public:
	virtual bool insert(const Key &key, const Value &value) = 0;
	virtual Value *find(const Key &key)                     = 0;
	virtual void reserve(size_t size) = 0; // For preallocation
										   // Add Erase, Size, etc.
	//[[nodiscard]] virtual size_t size() const = 0;

	virtual ~OptimizedHashMap() = default;
};
