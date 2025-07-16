#include "error.hpp"

#include <fmt/std.h>

std::string format_as(const Error &e) {
	return fmt::format("{} {}", e.message(), std::make_error_code(e.code()));
}

constexpr std::errc Error::code() const { return m_code; }

constexpr std::string_view Error::message() const { return m_message; }
