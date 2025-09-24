#pragma once

#include "simple_message.hpp"

#include <expected>

namespace redis {
template <typename T, typename E = RedisError>
using ErrorOr = std::expected<T, E>;
}
