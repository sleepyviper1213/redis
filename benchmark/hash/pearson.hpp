#pragma once
#include "utilities.hpp"

#include <array>
#include <cstddef>
#include <span>
#if defined(__x86_64__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

inline static constexpr std::array<u8, 256> pearson_table = {
	98,  6,   85,  150, 36,  23,  112, 164, 135, 103, 74,  1,   45,  12,  7,
	34,  67,  25,  63,  161, 1,   216, 97,  13,  85,  22,  93,  198, 78,  165,
	13,  65,  36,  12,  71,  92,  46,  11,  1,   98,  164, 28,  7,   36,  55,
	107, 79,  218, 72,  64,  227, 40,  73,  140, 145, 52,  87,  201, 60,  22,
	133, 177, 35,  16,  91,  62,  233, 31,  15,  169, 29,  93,  31,  109, 251,
	164, 12,  67,  205, 40,  143, 68,  99,  137, 203, 8,   190, 21,  206, 137,
	97,  201, 3,   224, 88,  132, 189, 27,  215, 144, 188, 132, 77,  14,  219,
	153, 208, 76,  25,  48,  114, 60,  245, 90,  44,  56,  99,  249, 34,  223,
	9,   199, 106, 207, 141, 211, 24,  59,  8,   13,  198, 30,  167, 182, 4,
	32,  15,  213, 193, 118, 26,  250, 157, 76,  173, 2,   139, 219, 119, 74,
	69,  3,   201, 255, 47,  182, 86,  53,  190, 202, 113, 16,  193, 99,  61,
	88,  88,  106, 214, 16,  8,   129, 115, 40,  166, 124, 165, 6,   173, 92,
	45,  200, 147, 233, 66,  10,  90,  20,  51,  199, 44,  219, 64,  7,   42,
	181, 241, 26,  148, 61,  19,  98,  203, 54,  202, 66,  31,  125, 152, 243,
	28,  35,  111, 158, 118, 200, 19,  79,  100, 156, 77,  240, 232, 164, 2,
	84,  17,  205, 46,  130, 59,  24,  126, 71,  14,  117, 21,  105, 180, 53,
	196, 37,  127, 170, 86,  203, 184, 225, 122, 38,  30,  9,   216, 141, 196,
	11};

class pearson64_hash {
public:
	explicit pearson64_hash(u64 seed = GOLDEN_GAMMA) : seed(seed) {}

	[[nodiscard]] uint64_t operator()(std::span<const char> s) const noexcept {
		return hash(s);
	}

	[[nodiscard]] uint64_t hash(std::span<const char> s) const noexcept {
#if defined(__x86_64__)
		__m128i hash_mask = _mm_cvtsi64_si128(0x0706'0504'0302'0100);

		__m128i hash = _mm_cvtsi64_si128(seed);

		// table lookup preparation
#if defined(__AES__) // AES-NI -------------
		__m128i ZERO = _mm_setzero_si128();
		__m128i ISOLATE_SBOX_MASK =
			_mm_set_epi32(0x0306'090C, 0x0F02'0508, 0x0B0E'0104, 0x070A'0D00);
#else  // ------------------------------------
		__m128i const p16  = _mm_set1_epi8(0x10);
		__m128i lut_result = _mm_xor_si128(lut_result, lut_result);
		__m128i selected_entries;
		__m128i table_line;
#endif // -----------------------------------

		for (char c : s) {
			// broadcast the character, xor into hash, make them different
			// permutations
			__m128i cc = _mm_set1_epi8(c);
			hash       = _mm_xor_si128(hash, cc);
			hash       = _mm_xor_si128(hash, hash_mask);

			// table lookup
#if defined(__AES__)                                 // AES-NI -------------
			hash =
				_mm_shuffle_epi8(hash,
								 ISOLATE_SBOX_MASK); // re-order along AES round
			hash = _mm_aesenclast_si128(hash, ZERO);
#else  // ------------------------------------
			size_t j;
			__m128i lut_index = hash;
			lut_result        = _mm_xor_si128(lut_result, lut_result);
			for (j = 0; j < 256; j += 16) {
				table_line       = _mm_load_si128((__m128i *)&pearson_table[j]);
				selected_entries = _mm_min_epu8(lut_index, p16);
				selected_entries = _mm_cmpeq_epi8(selected_entries, p16);
				selected_entries = _mm_or_si128(selected_entries, lut_index);
				selected_entries =
					_mm_shuffle_epi8(table_line, selected_entries);
				lut_result = _mm_or_si128(lut_result, selected_entries);
				lut_index  = _mm_sub_epi8(lut_index, p16);
			}
			hash = lut_result;
#endif // -----------------------------------
		}
		// store output
		return _mm_cvtsi128_si64(hash);
#elif defined(__aarch64__)
		// Initialize 16-lane vector; lower 8 lanes hold the 8 state bytes.
		// We'll store state in lanes 0..7 and keep lanes 8..15 zero (or
		// duplicates).
		uint8x16_t state_vec = vdupq_n_u8(0);
		{
			// fill lower 8 bytes from seed (little-endian canonicalization)
			u8 tmp[16]  = {};
			u64 tmpseed = seed;
			for (int i = 0; i < 8; ++i) {
				tmp[i] = static_cast<u8>(tmpseed & 0xFFu);
				tmpseed >>= 8;
			}
			// load into vector
			state_vec = vld1q_u8(tmp);
		}

		// Precompute 16 table lines of 16 bytes each (we can load them on
		// demand). pearson_table is contiguous 256 bytes; table_line_j starts
		// at &pearson_table[j*16]

		const uint8x16_t zero_vec = vdupq_n_u8(0);

		// temporary buffers for result
		uint8x16_t lut_index, lut_result, block_vec, offset_vec, selected, tbl,
			tbl_res;

		for (unsigned char cc : s) {
			const uint8x16_t cvec = vdupq_n_u8(static_cast<u8>(cc));
			// XOR input byte into each lane (broadcast)
			lut_index = veorq_u8(state_vec, cvec);

			// We'll compute per-byte lookup of pearson_table[lut_index[i]]
			// Strategy:
			//   block = lut_index >> 4        // value in 0..15
			//   offset = lut_index & 0x0F
			//   for j in 0..15:
			//     mask = (block == j)
			//     table_line = load 16 bytes from pearson_table + j*16
			//     res = vqtbl1q_u8(table_line, offset)
			//     lut_result |= res & mask

			// compute block and offset vectors
			block_vec = vshrq_n_u8(lut_index, 4);      // block = index >> 4
			offset_vec =
				vandq_u8(lut_index, vdupq_n_u8(0x0F)); // offset = index & 0x0F

			lut_result = vdupq_n_u8(0);                // zero

			// iterate 16 table blocks
			for (int block = 0; block < 16; ++block) {
				// load table line block*16 .. block*16+15
				const uint8_t *table_ptr = &pearson_table[block * 16];
				tbl                      = vld1q_u8(table_ptr);

				// compare block_vec == block -> mask: 0xFF where equal, 0x00
				// elsewhere
				uint8x16_t cmp =
					vceqq_u8(block_vec,
							 vdupq_n_u8(static_cast<uint8_t>(block)));

				// do table lookup with offsets: returns bytes where offset <16,
				// else zero vqtbl1q_u8 uses 4-bit indices into 16-byte table;
				// since offset_vec is 0..15 it's safe
				tbl_res = vqtbl1q_u8(tbl, offset_vec);

				// mask out lanes that are not from this block
				selected = vandq_u8(tbl_res, cmp);

				// OR into result
				lut_result = vorrq_u8(lut_result, selected);
			}

			// now lut_result contains pearson_table[lut_index[i]] for each lane
			state_vec = lut_result;
		}

		// Extract lower 8 bytes to produce u64 (little-endian packing)
		alignas(16) uint8_t out_bytes[16];
		vst1q_u8(out_bytes, state_vec);

		u64 out = 0;
		for (int i = 0; i < 8; ++i)
			out |= (static_cast<u64>(out_bytes[i]) << (i * 8));
		return out;
#else
		u8 h[8];
		for (size_t i = 0; i < 8; i++) h[i] = (seed >> (i * 8)) & 0xFF;

		for (char c : s)
			for (unsigned char &j : h) j = pearson_table[j ^ c];

		return ((uint64_t)h[0] << 56) | ((uint64_t)h[1] << 48) |
			   ((uint64_t)h[2] << 40) | ((uint64_t)h[3] << 32) |
			   ((uint64_t)h[4] << 24) | ((uint64_t)h[5] << 16) |
			   ((uint64_t)h[6] << 8) | ((uint64_t)h[7]);

#endif
	}

private:
	u64 seed;
};
