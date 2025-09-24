#pragma once
#include "simple_message.hpp"

#include <expected>
#include <functional>

namespace redis {
template <typename T, typename E = RedisError>
using ErrorRefOr = std::expected<std::reference_wrapper<T>, E>;
} // namespace redis
