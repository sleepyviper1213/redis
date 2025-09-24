#pragma once
#include <functional>
#include <optional>

template <typename T>
using OptionalRef = std::optional<std::reference_wrapper<T>>;
