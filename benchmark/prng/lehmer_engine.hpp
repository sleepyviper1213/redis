#pragma once
#include "splitmix64.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>

template <typename T>
	requires std::same_as<T, uint64_t> || std::same_as<T, __uint128_t>
class lehmer_engine {
public:
	using result_type = T;

	constexpr explicit lehmer_engine(uint64_t seed = 1) { seed_state(seed); }

	constexpr explicit lehmer_engine(std::seed_seq &seq) { seed(seq); }

	constexpr void seed(uint64_t seedval = 1) { seed_state(seedval); }

	constexpr void seed(std::seed_seq &seq) {
		uint64_t s[2];
		seq.generate(s, s + 2);
		state_ = (static_cast<__uint128_t>(s[0]) << 64) | s[1];
		if (state_ == 0) state_ = 1;
	}

	constexpr result_type operator()() {
		constexpr __uint128_t multiplier_ = 0xda94'2042'e4dd'58b5ULL;
		state_ *= multiplier_;
		if constexpr (std::same_as<T, uint64_t>)
			return static_cast<result_type>(state_ >> 64);
		else if constexpr (std::same_as<T, __uint128_t>) return state_;
	}

	// discard like other engines
	constexpr void discard(unsigned long long n) {
		for (unsigned long long i = 0; i < n; ++i) (*this)();
	}

	static constexpr result_type min() {
		return std::numeric_limits<result_type>::min();
	}

	static constexpr result_type max() {
		return std::numeric_limits<result_type>::max();
	}

private:
	constexpr void seed_state(uint64_t seedval) {
		SplitMix64 sm(seedval);
		uint64_t lo = sm();
		uint64_t hi = sm();
		state_      = (static_cast<__uint128_t>(hi) << 64) | lo;
		if (state_ == 0) state_ = 1;
	}

	__uint128_t state_{};
};

using lehmer64_engine  = lehmer_engine<uint64_t>;
using lehmer128_engine = lehmer_engine<__uint128_t>;
