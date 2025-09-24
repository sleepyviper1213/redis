#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#if defined(__x86_64__)
#include <crc32intrin.h>
#elif defined(__aarch64__)
#include <arm_acle.h>
#endif
template <size_t N>
constexpr auto pack(const std::span<const char, N> &bytes) noexcept {
	static_assert(N == 2 || N == 4 || N == 8,
				  "pack only supports 16, 32, or 64-bit integers");
	using result_t = std::conditional_t<
		N == 2,
		std::uint16_t,
		std::conditional_t<N == 4, std::uint32_t, std::uint64_t>>;
	result_t value = 0;
	for (std::size_t i = 0; i < N; ++i)
		value |= static_cast<result_t>(bytes[i]) << (i * 8);
	return value;
}

class crc32_hasher {
public:
	constexpr explicit crc32_hasher(uint32_t seed = 0) : seed(seed) {}

	[[nodiscard]] uint32_t operator()(std::string_view s) const noexcept {
		return hash(s);
	}

	[[nodiscard]] uint32_t hash(std::string_view s) const noexcept {
		uint32_t hash = seed;
		for (auto c : s) {
#if defined(__x86_64__)
			hash = _mm_crc32_u32(hash, static_cast<u8>(c));
#elif defined(__aarch64__)
			hash = __crc32b(hash, c);
#endif
		}
		return hash;
	}

	[[nodiscard]] uint32_t hash(std::span<const char, 4> s) const noexcept {
		uint32_t hash = seed;
		const auto x  = pack(s);
#if defined(__x86_64__)
		hash = _mm_crc32_u32(hash, x);
#elif defined(__aarch64__)
		hash = __crc32w(hash, x);
#endif
		return hash;
	}

private:
	uint32_t seed;
};
