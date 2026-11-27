#pragma once

#include <cstddef>

namespace redis {

/// Byte counts, so a size reads as `64 * KiB` and not `64 * 1024`. Binary
/// (power of 1024) only: nothing here is measured in decimal units.
inline constexpr std::size_t KiB = 1024;
inline constexpr std::size_t MiB = 1024 * KiB;

} // namespace redis
