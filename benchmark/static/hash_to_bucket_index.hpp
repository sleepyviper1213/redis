#pragma once
#include <cstdint>
#include <utility>
enum class hash_to_index_strategy {
	modulo,
	shiftless, // Requires power of 2 table size.
};

inline std::size_t hash_to_index(uint32_t hash, uint32_t table_size,
							std::uint32_t entry_size,
							hash_to_index_strategy strategy) {
	switch (strategy) {
	case hash_to_index_strategy::modulo: return hash % table_size;
	case hash_to_index_strategy::shiftless:
		return hash % (table_size * entry_size) / entry_size;
	default: std::unreachable();
	}
}