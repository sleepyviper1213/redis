#pragma once
#include "utilities.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#if defined(__x86_64__)
#include <immintrin.h> // SSSE3 pshufb
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif


class aes_hash {
public:
	explicit aes_hash(u64 seed = GOLDEN_GAMMA) noexcept {
#if defined(__x86_64__)
		state_ = _mm_set_epi64x(seed, seed ^ 0xDEAD'BEEF'DEAD'BEEF);
#elif defined(__aarch64__)
		const u64 v[2] = {seed, seed ^ 0xDEAD'BEEF'DEAD'BEEF};
		state_         = vld1q_u64(v);
#endif
	}

	size_t operator()(std::string_view s) const noexcept {
		constexpr size_t block_size = 16;
		size_t full_blocks          = s.size() / block_size;
		auto local_state            = state_;

		for (size_t i = 0; i < full_blocks; ++i) {
			const auto *ptr = s.data() + (i * block_size);
#if defined(__x86_64__)
			__m128i block;
			std::memcpy(&block, ptr, block_size);
			local_state = _mm_aesenc_si128(local_state, block);
#elif defined(__aarch64__)
			uint8x16_t block;
			std::memcpy(&block, ptr, block_size);
			local_state = vaeseq_u8(local_state, block);
#endif
		}

		if (const size_t rem = s.size() % block_size; rem > 0) {
			std::array<u8, 16> last{};
			std::memcpy(last.data(), s.data() + full_blocks * block_size, rem);
			last[rem] = 0x80;
#if defined(__x86_64__)
			__m128i block;
			std::memcpy(&block, last.data(), block_size);
			local_state = _mm_aesenc_si128(local_state, block);
#elif defined(__aarch64__)
			uint8x16_t block;
			std::memcpy(&block, last.data(), block_size);
			local_state = vaeseq_u8(local_state, block);
#endif
		}

		alignas(16) u64 parts[2];
#if defined(__x86_64__)
		_mm_store_si128(reinterpret_cast<__m128i *>(parts), local_state);
#elif defined(__aarch64__)
		vst1q_u64(parts, local_state);
#endif
		return static_cast<size_t>(parts[0] ^ parts[1]);
	}

private:
#if defined(__x86_64__)
	__m128i state_;
#elif defined(__aarch64__)
	uint8x16_t state_;
#endif
};
