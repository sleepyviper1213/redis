#pragma once
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__x86_64__)
#include <nmmintrin.h>
#endif
#include "interface.hpp"

#include <cstring> // memcmp
#include <string>
#include <vector>

// Assume fixed slot size: 32 bytes (key hash + key len + key bytes + value ptr)
constexpr size_t SLOT_SIZE   = 32;
constexpr size_t TABLE_SIZE  = 1ULL << 20; // Power of 2
constexpr size_t LOAD_FACTOR = 75;         // %

class CustomSimdHashMap : public OptimizedHashMap<std::string, std::string> {
public:
	CustomSimdHashMap()
		: table_(TABLE_SIZE * SLOT_SIZE), hash_mask_(TABLE_SIZE - 1) {}

	bool insert(const std::string &key, const std::string &value) override {
		if (count_ * 100 / TABLE_SIZE > LOAD_FACTOR) rehash(); // Grow table
		uint64_t h    = hash(key);
		size_t idx    = h & hash_mask_;
		uint8_t *slot = &table_[idx * SLOT_SIZE];

		// Linear probe with SIMD for batches
		size_t probe_count = 0;
		while (true) {
			if (memcmp(slot, key.data(), key.size()) ==
				0) { // Occupied by same key
				// Update value
				return true;
			}
			if (is_empty_slot(slot)) {
				// Copy key + value to slot
				memcpy(slot, key.data(), key.size());
				// Store value ptr, etc.
				++count_;
				return true;
			}
			idx  = (idx + 1) & hash_mask_;
			slot = &table_[idx * SLOT_SIZE];
			probe_count++;
			if (probe_count > TABLE_SIZE) return false; // Full
		}
	}

	std::string *find(const std::string &key) override {
		uint64_t h         = hash(key);
		size_t idx         = h & hash_mask_;
		uint8_t *slot      = &table_[idx * SLOT_SIZE];
		size_t probe_count = 0;

		// Batch probe with intrinsics
		while (probe_count < 4) {
			if (vectorized_probe(slot - probe_count * SLOT_SIZE, key, idx)) {
				// Extract value from slot
				return new std::string(/* from slot */);
			}
			probe_count += 4;
		}
		// Fallback scalar probe...
		return nullptr;
	}

	void reserve(size_t size) override {
		size_t new_size = next_power_of_two(size * 100 / LOAD_FACTOR);
		// Rehash logic: copy slots, update mask
	}

private:
	std::vector<uint8_t> table_; // Raw byte storage
	size_t count_ = 0;
	size_t hash_mask_;

	// Custom hash function (uint64_t hash(const std::string& key))
	[[nodiscard]] uint64_t
	hash(const std::string &key) const { /* See section 2 */
		return 0;
	}

	// Vectorized probe: Check 4 slots at once for key match
	bool vectorized_probe(const uint8_t *probe_start, const std::string &key,
						  size_t &slot_idx) const {
#if defined(__aarch64__)
#elif defined(__x86_64__)
		__m256i key_vec = _mm256_loadu_si256(
			reinterpret_cast<const __m256i *>(key.data())); // Assume key padded
		for (size_t i = 0; i < 4; ++i) {                    // Probe 4 slots
			__m256i slot_vec = _mm256_loadu_si256(probe_start + i * SLOT_SIZE);
			if (_mm256_movemask_epi8(_mm256_cmpeq_epi8(key_vec, slot_vec)) ==
				0xFFFF) {                                   // Full match
				slot_idx = (probe_start - table_.data()) / SLOT_SIZE + i;
				return true;
			}
		}
#endif
		return false;
	}

	bool is_empty_slot(const uint8_t *slot) const {
		return slot[0] == 0;
	} // Tombstone or empty marker

	void rehash() { /* Double size, reinsert all */ }

	size_t next_power_of_two(size_t n) { /* Bit hack */ }
};
