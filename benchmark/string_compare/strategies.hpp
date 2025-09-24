#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>

#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__x86_64__)
#include <immintrin.h>
#include <nmmintrin.h>
#endif


struct CmpMemcmp {
	bool operator()(std::string_view a, std::string_view b) const noexcept {
		if (a.size() != b.size()) return false;
		return std::memcmp(a.data(), b.data(), a.size()) == 0;
	}
};

struct CmpCheckFirstThenMemcmp {
	bool operator()(std::string_view a, std::string_view b) const noexcept {
		if (a.size() != b.size()) return false;
		if (a.size() < sizeof(uint64_t))
			return std::memcmp(a.data(), b.data(), a.size()) == 0;

		uint64_t ua, ub;
		std::memcpy(&ua, a.data(), sizeof(ua));
		std::memcpy(&ub, b.data(), sizeof(ub));
		if (ua != ub) return false;
		return std::memcmp(a.data() + sizeof(ua),
						   b.data() + sizeof(ub),
						   a.size() - sizeof(ua)) == 0;
	}
};

struct CmpCheckTwoThenMemcmp {
	bool operator()(std::string_view a, std::string_view b) const noexcept {
		if (a.size() != b.size()) return false;
		if (a.size() < 16) return CmpCheckFirstThenMemcmp{}(a, b);

		uint64_t u1a, u1b, u2a, u2b;
		std::memcpy(&u1a, a.data(), 8);
		std::memcpy(&u1b, b.data(), 8);
		if (u1a != u1b) return false;
		std::memcpy(&u2a, a.data() + 8, 8);
		std::memcpy(&u2b, b.data() + 8, 8);
		if (u2a != u2b) return false;
		return std::memcmp(a.data() + 16, b.data() + 16, a.size() - 16) == 0;
	}
};


#ifdef __x86_64__
struct SSE {
	bool operator()(std::string_view a, std::string_view b) const noexcept {
		__m128i entry_unmasked = ::_mm_lddqu_si128((const __m128i *)a.data());
		__m128i identifier_unmasked =
			::_mm_lddqu_si128((const __m128i *)b.data());
		// Calculating the mask this way seems to be much much faster than '(1
		// << size) - 1'.
		std::uint32_t inv_mask   = ~(std::uint32_t)0 << size;
		std::uint32_t mask       = ~inv_mask;
		std::uint32_t equal_mask = ::_mm_movemask_epi8(
			::_mm_cmpeq_epi8(entry_unmasked, identifier_unmasked));
		std::uint32_t not_equal_mask = ~equal_mask;
	}
};

struct CmpPcmpistri {
	bool operator()(std::string_view a, std::string_view b) const noexcept {
		if (a.size() != b.size()) return false;
		size_t i = 0;
		while (a.size() - i >= 16) {
			__m128i va = _mm_loadu_si128((const __m128i *)(a.data() + i));
			__m128i vb = _mm_loadu_si128((const __m128i *)(b.data() + i));
			int res =
				_mm_cmpistri(vb, va, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ORDERED);
			if (res != 16) return false; // mismatch inside this 16-byte block
			i += 16;
		}
		if (i < a.size())
			return std::memcmp(a.data() + i, b.data() + i, a.size() - i) == 0;
		return true;
	}
};
#elif defined(__aarch64__)
struct CmpNeonMask {
	using is_transparent = void;

	bool operator()(std::string_view a, std::string_view b) const noexcept {
		if (a.size() != b.size()) return false;
		size_t i = 0;
		while (a.size() - i >= 16) {
			uint8x16_t va = vld1q_u8((const uint8_t *)(a.data() + i));
			uint8x16_t vb = vld1q_u8((const uint8_t *)(b.data() + i));
			uint8x16_t eq = vceqq_u8(va, vb);
			if (vminvq_u8(eq) != 0xFF) return false;
			i += 16;
		}
		if (i < a.size())
			return std::memcmp(a.data() + i, b.data() + i, a.size() - i) == 0;
		return true;
	}
};

struct X {
	using is_transparent = void;

	bool operator()(std::string_view a, std::string_view b) const noexcept {
		const ::uint8x16_t zero_to_fifteen = {
			0,
			1,
			2,
			3,
			4,
			5,
			6,
			7,
			8,
			9,
			10,
			11,
			12,
			13,
			14,
			15,
		};

		::uint8x16_t mask = ::vcgtq_u8(::vdupq_n_u8(b.size()), zero_to_fifteen);
		::uint8x16_t entry_unmasked;
		std::memcpy(&entry_unmasked, a.data(), 16);
		::uint8x16_t identifier_unmasked;
		std::memcpy(&identifier_unmasked, b.data(), 16);
		::uint8x16_t compared =
			::vandq_u8(::veorq_u8(entry_unmasked, identifier_unmasked), mask);

		if ((vgetq_lane_s64(compared, 0) | vgetq_lane_s64(compared, 1)) != 0)
			return false;
		return true;
	}
};
#endif

struct chunk_8_4 {
	using is_transparent = void;

	bool operator()(std::string_view identifier,
					std::string_view entry_keyword) const noexcept {
		std::uint64_t identifier_first_8;
		std::memcpy(&identifier_first_8, identifier.data(), 8);
		std::uint32_t identifier_last_4;
		std::memcpy(&identifier_last_4, identifier.data() + 8, 4);

		std::uint64_t entry_first_8;
		std::memcpy(&entry_first_8, entry_keyword.data(), 8);
		std::uint32_t entry_last_4;
		std::memcpy(&entry_last_4, entry_keyword.data() + 8, 4);
#if 0
    // FIXME(strager): GCC emits jumps for this code. Clang emits cmov, which is
    // much better. We should coerce GCC into generating cmov.
    std::uint64_t first_8_mask =
        identifier.size() >= 8
        ? UINT64_MAX
        : (std::uint64_t(1) << (identifier.size() * 8)) - 1;
    std::uint32_t last_4_mask =
        identifier.size() <= 8
        ? 0
        : (std::uint32_t(1) << ((identifier.size()-8) * 8)) - 1;
#else
		__uint128_t const mask =
			(__uint128_t(1) << (identifier.size() * 8)) - 1;
		const std::uint64_t first_8_mask = mask;
		const std::uint32_t last_4_mask  = mask >> (8 * 8);
#endif


		std::uint64_t const first_8_comparison =
			((identifier_first_8 & first_8_mask) ^
			 (entry_first_8 & first_8_mask));
		const std::uint64_t last_4_comparison =
			((identifier_last_4 & last_4_mask) ^ (entry_last_4 & last_4_mask));

		if ((first_8_comparison | last_4_comparison)) return false;
		return true;
	}
};