#include "core/error/simple_message.hpp"

#include "core/error/errc_formatter.hpp"

#include <fmt/format.h>

namespace redis {
SimpleMessage::SimpleMessage(std::string_view message, std::errc code)
	: context_message_(message), code_(code) {}

std::errc SimpleMessage::code() const { return code_; }

std::string_view SimpleMessage::context_message() const {
	return context_message_;
}

bool SimpleMessage::contains_error_message(std::string_view msg) const {
	return context_message_.contains(msg);
}

std::string format_as(const SimpleMessage &e) {
	return fmt::format("[{}] {}", e.code(), e.context_message());
}
} // namespace redis
