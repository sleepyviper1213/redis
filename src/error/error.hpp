#pragma once

#include <fmt/base.h>

#include <cassert>
#include <expected>
#include <optional>
#include <string_view>
#include <system_error>

class [[nodiscard]] Error {
public:
	constexpr Error(std::string_view message, std::errc code)
		: message_(message), code_(code) {}

	constexpr auto operator<=>(const Error &other) const = default;

	[[nodiscard]] constexpr std::errc code() const;

	[[nodiscard]] constexpr std::string_view message() const;

	[[nodiscard]] constexpr bool
	containsErrorMessage(std::string_view msg) const {
		return message_.contains(msg);
	}

private:
	std::string_view message_;

	std::errc code_{};
};

template <typename... Args>
constexpr auto failed(Args &&...args) {
	return std::unexpected<Error>{std::in_place, std::forward<Args>(args)...};
}

template <typename T>
constexpr std::expected<T, Error>
ok_or(const std::optional<T> &opt, const std::string &msg, std::errc code) {
	if (opt) return *opt;
	return failed(msg, code);
}

std::string format_as(const Error &e);
