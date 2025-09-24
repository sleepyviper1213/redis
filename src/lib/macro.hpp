#pragma once
#define ALWAYS_INLINE __attribute__((always_inline))
#include <type_traits>

template <typename T>
using AddConstToReferencedType = const std::remove_reference_t<T> &;
