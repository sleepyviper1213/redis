#pragma once
#include <cstddef>
#include <cstdint>
// --------------------------- Utilities ---------------------------
using u8    = uint8_t;
using u32   = uint32_t;
using u64   = uint64_t;
using usize = size_t;

// 2^64 / φ (golden ratio), see Knuth MMIX hashing
constexpr u64 GOLDEN_GAMMA = 0x9e37'79b9'7f4a'7c15ULL;
