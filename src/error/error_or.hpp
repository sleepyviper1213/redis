#pragma once
#include "error.hpp"
template <typename T>
using ErrorOr = std::expected<T, Error>;
