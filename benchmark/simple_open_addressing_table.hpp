#pragma once
// --------------------------- Simple Open-Addressing Table
// --------------------------- We'll build a small fixed-size table
// (power-of-two) with linear probing for the experiment. Keys are stored as
// continuous strings in a vector and we keep pointer+length in entry.

#include "hash_strategies.hpp"
#include "string_compare_strategies.hpp"
#include "utilities.hpp"

struct Bucket {
	const char *ptr = nullptr;
	usize len       = 0;
	int value       = -1;
	bool occupied   = false;
};

struct MiniTable {
	std::vector<Bucket> table;
	usize capacity_mask;
	HashFn hash_fn;
	CmpFn cmp_fn;

	MiniTable(usize capacity_pow2, HashFn h, CmpFn cmp)
		: table(capacity_pow2),
		  capacity_mask(capacity_pow2 - 1),
		  hash_fn(h),
		  cmp_fn(cmp) {}

	bool insert(const std::string &key, int value,
				const std::vector<char> &key_store) {
		u64 h     = hash_fn(key.data(), key.size());
		usize idx = (usize)h & capacity_mask;
		for (usize tries = 0;; tries++) {
			Bucket &b = table[idx];
			if (!b.occupied) {
				// store pointer into key_store (we expect key_store to
				// outlive the table)
				b.ptr      = key.data();
				b.len      = key.size();
				b.value    = value;
				b.occupied = true;
				return true;
			} else {
				// if same key, update
				if (cmp_fn(b.ptr, b.len, key.data(), key.size())) {
					b.value = value;
					return true;
				}
				idx = (idx + 1) & capacity_mask;
			}
			if (tries > capacity_mask) return false; // table full
		}
	}

	std::optional<int> lookup(const std::string &key) const {
		u64 h     = hash_fn(key.data(), key.size());
		usize idx = (usize)h & capacity_mask;
		for (usize tries = 0;; tries++) {
			const Bucket &b = table[idx];
			if (!b.occupied) return std::nullopt;
			if (cmp_fn(b.ptr, b.len, key.data(), key.size())) return b.value;
			idx = (idx + 1) & capacity_mask;
			if (tries > capacity_mask) return std::nullopt;
		}
	}
};
