#pragma once
#include "detail/basic_simple_dynamic_string.hpp"

namespace redis {

/// An sds-style string with the default allocator.
using SimpleDynamicString = detail::BasicSimpleDynamicString<char>;

} // namespace redis
