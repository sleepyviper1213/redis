#include "error.hpp"

#include <fmt/std.h>

#include <cstring>

std::string format_as(const Error &e) {
	return fmt::format("[{}] {}", std::make_error_code(e.code()), e.message());
}

constexpr std::errc Error::code() const { return code_; }

constexpr std::string_view Error::message() const { return message_; }
