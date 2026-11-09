#pragma once
#include <cstdint>

namespace redis::detail {

/// The type of a key's value. The order is the snapshot format's type byte.
enum class ObjectType : uint8_t { String, List, Set, Hash };

} // namespace redis::detail
