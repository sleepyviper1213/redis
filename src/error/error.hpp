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
		: m_message(message), m_code(code) {}

	constexpr auto operator<=>(const Error &other) const = default;

	[[nodiscard]] constexpr std::errc code() const;

	[[nodiscard]] constexpr std::string_view message() const;

protected:
private:
	std::string_view m_message;

	std::errc m_code;
};

template <typename... Args>
constexpr auto failed(Args &&...args) {
	return std::unexpected<Error>{std::in_place, std::forward<Args>(args)...};
}

template <typename T>
constexpr std::expected<T, Error> ok_or(const std::optional<T> &opt,
										std::string_view msg, std::errc code) {
	if (opt) return *opt;
	return failed(msg, code);
}

std::string format_as(const Error &e);