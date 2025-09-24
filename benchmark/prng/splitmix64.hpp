#pragma once
#include "../hash/utilities.hpp"

#include <cstdint>

/**
 * \brief expand a 64-bit seed into high-quality entropy for Lehmer generators
 */
class SplitMix64 {
public:
	constexpr explicit SplitMix64(uint64_t seed) : state(seed) {}

	constexpr uint64_t next() noexcept {
		uint64_t z = (state += GOLDEN_GAMMA);
		z          = (z ^ (z >> 30)) * 0xbf58'476d'1ce4'e5b9ULL;
		z          = (z ^ (z >> 27)) * 0x94d0'49bb'1331'11ebULL;
		return z ^ (z >> 31);
	}

	constexpr uint64_t operator()() noexcept { return next(); }

private:
	uint64_t state;
};
