#include "common/resp.hpp"
#include "core/error.hpp"
#include "core/utils/parse_number.hpp"

#include <fmt/base.h>
#include <fmt/compile.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace redis::resp {
Value Value::from_simple_string(std::string s) {
	return {Type::SimpleString, std::move(s)};
}

Value Value::from_simple_error(std::string s) {
	return {Type::SimpleError, std::move(s)};
}

Value Value::from_integer(int64_t value) { return {Type::Integer, value}; }

Value Value::from_bulk_string(std::string s) {
	return {Type::BulkString, std::move(s)};
}

int64_t Value::as_integer() const {
	assert(is_integer());
	return std::get<int64_t>(data_);
}

std::expected<int64_t, std::errc> Value::try_as_integer() const {
	if (is_integer()) return std::get<int64_t>(data_);
	return TRY(parse_integer(as_string()));
}

const Value::Array &Value::as_array() const {
	assert(is_array());
	return std::get<Array>(data_);
}

const std::string &Value::as_string() const {
	// Not inline in the assert: the commas of a braced list would split its
	// macro arguments.
	[[maybe_unused]] constexpr std::array TEXT_TYPES{Type::SimpleString,
													 Type::BulkString,
													 Type::SimpleError};
	assert(std::ranges::contains(TEXT_TYPES, type_));

	assert(is_string());
	return std::get<std::string>(data_);
}

bool Value::is_null() const { return type_ == Type::Null; }

bool Value::is_error() const {
	return type_ == Type::SimpleError && is_string();
}

bool Value::is_string() const {
	return std::holds_alternative<std::string>(data_);
}

bool Value::is_bulk_string() const {
	return type_ == Type::BulkString && is_string();
}

bool Value::is_simple_string() const {
	return type_ == Type::SimpleString && is_string();
}

bool Value::is_array() const {
	return type_ == Type::Array && std::holds_alternative<Array>(data_);
}

Value::Value(Type type) : type_(type) {}

Value Value::from_null() { return {Type::Null}; }

Value Value::from_double(double d) { return {Type::Double, d}; }

Value Value::from_raw_array(std::span<const std::string> list) {
	Array arr;
	arr.reserve(list.size());
	for (const auto &s : list) arr.emplace_back(Value::from_bulk_string(s));
	return {Type::Array, std::move(arr)};
}

Value::Type Value::type() const { return type_; }

Value Value::from_array(Array list) { return {Type::Array, std::move(list)}; }

double Value::as_double() const { return std::get<double>(data_); }

bool Value::is_integer() const {
	return type_ == Type::Integer && std::holds_alternative<int64_t>(data_);
}

bool Value::is_double() const {
	return type_ == Type::Double && std::holds_alternative<double>(data_);
}
} // namespace redis::resp

using redis::resp::Value;

namespace {
// A simple string or error ends at the first CRLF, so one inside the text (a
// command name or argument echoed back, say) would end the reply early and let
// the rest pass as a reply of its own. Like Redis, turn them into spaces.
//
// The wire format strings below are FMT_COMPILE: every reply goes through
// them, and a runtime format string is parsed again on each call.
fmt::format_context::iterator write_line(fmt::format_context::iterator it,
										 char prefix, std::string_view text) {
	if (text.find_first_of("\r\n") == std::string_view::npos)
		return fmt::format_to(it, FMT_COMPILE("{}{}\r\n"), prefix, text);
	std::string clean(text);
	std::ranges::replace(clean, '\r', ' ');
	std::ranges::replace(clean, '\n', ' ');
	return fmt::format_to(it, FMT_COMPILE("{}{}\r\n"), prefix, clean);
}
} // namespace

auto fmt::formatter<Value>::format(const Value &value,
								   format_context &ctx) const
	-> format_context::iterator {
	using enum Value::Type;

	auto it = ctx.out();
	switch (presentation) {
	case '?':
		switch (value.type()) {
		case SimpleString:
			it = fmt::format_to(it, "{}", value.as_string());
			break;
		case SimpleError:
			it = fmt::format_to(it, "(error) {}", value.as_string());
			break;
		case Integer:
			it = fmt::format_to(it, "(integer) {}", value.as_integer());
			break;
		case BulkString: {
			it = fmt::format_to(it, "{:?}", value.as_string());
			break;
		}
		case Array: {
			const auto &arr = value.as_array();
			if (arr.empty()) it = fmt::format_to(it, "(empty array)");
			else it = fmt::format_to(it, "(array) {:?}", fmt::join(arr, " "));
			break;
		}
		case Null: it = fmt::format_to(it, "(null)"); break;
		case Double:
			it = fmt::format_to(it, "(double) {}", value.as_double());
			break;
		default: std::unreachable();
		}
		break;
	case 'e':
	case 0:
		switch (value.type()) {
		case SimpleString: it = write_line(it, '+', value.as_string()); break;
		case SimpleError: it = write_line(it, '-', value.as_string()); break;
		case Integer:
			it = fmt::format_to(it, FMT_COMPILE(":{}\r\n"), value.as_integer());
			break;
		case BulkString: {
			const auto &str = value.as_string();
			it = fmt::format_to(it, FMT_COMPILE("${}\r\n{}\r\n"), str.size(), str);
			break;
		}
		case Array: {
			const auto &arr = value.as_array();
			it              = fmt::format_to(it,
											 FMT_COMPILE("*{}\r\n{:e}"),
											 arr.size(),
											 fmt::join(arr, ""));
			break;
		}
		// RESP2 null bulk string. RESP3's "_" is only legal after a client
		// negotiates protocol 3 with HELLO, which this server does not offer.
		case Null: it = fmt::format_to(it, FMT_COMPILE("$-1\r\n")); break;
		case Double:
			it = fmt::format_to(it, FMT_COMPILE(",{}\r\n"), value.as_double());
			break;
		default: std::unreachable();
		}
		break;
	default: std::unreachable();
	}
	return it;
}
