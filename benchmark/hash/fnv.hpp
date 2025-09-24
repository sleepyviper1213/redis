#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

class Fnv1a32Hasher {
public:
	[[nodiscard]] constexpr uint32_t
	hash(std::span<const char> s) const noexcept {
		constexpr uint32_t FNV_PRIME = 0x0100'0193;
		uint32_t hash                = seed; // seed
		for (auto c : s) hash = (hash ^ c) * FNV_PRIME;
		return hash;
	}

	constexpr size_t operator()(std::span<const char> s) const noexcept {
		return hash(s);
	}

private:
	uint64_t seed = 0x811c'9dc5;
};

class Fnv1a64Hasher {
public:
	[[nodiscard]] constexpr uint64_t
	hash(std::span<const char> s) const noexcept {
		constexpr uint64_t FNV_PRIME = 0x0000'0100'0000'01b3;
		uint64_t hash                = seed;
		for (auto c : s) hash = (hash ^ c) * FNV_PRIME;
		return hash;
	}

	constexpr size_t operator()(std::span<const char> s) const noexcept {
		return hash(s);
	}

private:
	uint64_t seed = 0xcbf2'9ce4'8422'2325;
};
