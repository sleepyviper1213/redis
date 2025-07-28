#pragma once
#include "error.hpp"
#include "utils/lifetimebound.hpp"

#include <concepts>
#include <expected>
#include <optional>
#include <utility>

template <typename T>
using ErrorOr = std::expected<T, Error>;

template <typename... Args>
	requires std::constructible_from<Error, Args &&...>
constexpr auto failed(LIFETIMEBOUND Args &&...args) {
	return std::unexpected<Error>{std::in_place, std::forward<Args>(args)...};
}

template <typename T, typename... Args>
constexpr ErrorOr<T> ok_or(const std::optional<T> &opt, Args &&...args) {
	if (opt) return *opt;
	return failed(std::forward<Args>(args)...);
}
