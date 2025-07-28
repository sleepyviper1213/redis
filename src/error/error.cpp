#include "error.hpp"

#include <fmt/std.h>

#include <system_error>

Error::Error(std::string_view message, std::errc code)
	: context_message_(message), code_(code) {}

std::errc Error::code() const { return code_; }

std::string_view Error::contextMessage() const { return context_message_; }

bool Error::containsErrorMessage(std::string_view msg) const {
	return context_message_.contains(msg);
}

std::string format_as(const Error &e) {
	return fmt::format("[{}] {}",
					   std::make_error_code(e.code()),
					   e.contextMessage());
}
