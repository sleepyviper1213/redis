#pragma once

#include <concepts>

namespace AK {

template <typename T, typename... Us>
concept one_of = (std::same_as<T, Us> || ...);
} // namespace AK
