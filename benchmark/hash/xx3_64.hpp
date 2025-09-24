#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <xxhash.h>

class Xxh3_64Hasher {
public:
	constexpr explicit Xxh3_64Hasher(uint64_t seed = 0) : seed(seed) {}

	[[nodiscard]] uint64_t operator()(std::span<const char> s) const noexcept {
		return hash(s);
	}

	[[nodiscard]] uint64_t hash(std::span<const char> s) const noexcept {
		return XXH3_64bits_withSeed(s.data(), s.size(), seed);
	}

private:
	uint64_t seed;
};
