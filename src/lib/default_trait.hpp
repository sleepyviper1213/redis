#pragma once
#include <type_traits>
template <typename T, typename U>
inline constexpr bool IsHashCompatible = false;
template <typename T>
inline constexpr bool IsHashCompatible<T, T> = true;

template <typename T, typename U>
concept HashCompatible = IsHashCompatible<std::decay_t<T>, std::decay_t<U>>;

template <typename T>
struct DefaultTraits {
	using PeekType      = T &;
	using ConstPeekType = const T &;

	static constexpr bool is_trivial() { return false; }

	static constexpr bool is_trivially_serializable() { return false; }

	static constexpr bool equals(const T &a, const T &b) { return a == b; }

	template <HashCompatible<T> U>
	static bool equals(const T &self, const U &other) {
		return self == other;
	}
};
