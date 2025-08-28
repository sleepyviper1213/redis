#pragma once

#include <concepts>

namespace redis {
template <class Src, class Dest>
	requires (!std::same_as<Src, Dest>)
struct From;

template <typename Src, typename Dst>
concept FromConvertible = !std::convertible_to<Src, Dst> && requires(Src s) {
	{ From<Src, Dst>::convert(s) } noexcept -> std::same_as<Dst>;
};
} // namespace redis
