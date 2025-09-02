#include "core/error.hpp"
#include "core/resp.hpp"

#include <fmt/ranges.h>

#include <cassert>
#include <utility>
#include <variant>
#include <vector>

namespace redis::resp {
Value Value::from_simple_string(std::string s) {
	return {Type::SIMPLE_STRING, std::move(s)};
}

Value Value::from_simple_error(std::string s) {
	return {Type::SIMPLE_ERROR, std::move(s)};
}

Value Value::from_integer(int64_t value) { return {Type::INTEGEER, value}; }

Value Value::from_bulk_string(std::string s) {
	return {Type::BULK_STRING, std::move(s)};
}

int64_t Value::as_integer() const {
	assert(is_integer());
	return std::get<int64_t>(data_);
}

std::expected<int64_t, std::errc> Value::try_as_integer() const {
	if (is_integer()) return std::get<int64_t>(data_);
	return TRY(Parser::parse_integer(as_string()));
}

const Value::Array &Value::as_array() const {
	assert(is_array());
	return std::get<Array>(data_);
}

const std::string &Value::as_string() const {
	assert(type_ == Type::SIMPLE_STRING || type_ == Type::BULK_STRING ||
		   type_ == Type::SIMPLE_ERROR);

	assert(is_string());
	return std::get<std::string>(data_);
}

bool Value::is_null() const { return type_ == Type::NULLABLE; }

bool Value::is_error() const {
	return type_ == Type::SIMPLE_ERROR && is_string();
}

bool Value::is_string() const {
	return std::holds_alternative<std::string>(data_);
}

bool Value::is_bulk_string() const {
	return type_ == Type::BULK_STRING && is_string();
}

bool Value::is_simple_string() const {
	return type_ == Type::SIMPLE_STRING && is_string();
}

bool Value::is_array() const {
	return type_ == Type::ARRAY && std::holds_alternative<Array>(data_);
}

Value::Value(Type type) : type_(type) {}

Value Value::from_null() { return {Type::NULLABLE}; }

Value Value::from_double(double d) { return {Type::DOUBLE, d}; }

Value Value::from_raw_array(std::span<const std::string> list) {
	Array arr;
	arr.reserve(list.size());
	for (const auto &s : list) arr.emplace_back(Value::from_bulk_string(s));
	return {Type::ARRAY, std::move(arr)};
}

Value::Type Value::type() const { return type_; }

Value Value::from_array(Array list) { return {Type::ARRAY, std::move(list)}; }

double Value::as_double() const { return std::get<double>(data_); }

bool Value::is_integer() const {
	return type_ == Type::INTEGEER && std::holds_alternative<int64_t>(data_);
}

bool Value::is_double() const {
	return type_ == Type::DOUBLE && std::holds_alternative<double>(data_);
}
} // namespace redis::resp

using redis::resp::Value;

auto fmt::formatter<Value>::format(const Value &value,
								   format_context &ctx) const
	-> format_context::iterator {
	using enum Value::Type;

	auto it = ctx.out();
	switch (presentation) {
	case '?':
		switch (value.type()) {
		case SIMPLE_STRING:
			it = fmt::format_to(it, "{}", value.as_string());
			break;
		case SIMPLE_ERROR:
			it = fmt::format_to(it, "(error) {}", value.as_string());
			break;
		case INTEGEER:
			it = fmt::format_to(it, "(integer) {}", value.as_integer());
			break;
		case BULK_STRING: {
			it = fmt::format_to(it, "{:?}", value.as_string());
			break;
		}
		case ARRAY: {
			const auto &arr = value.as_array();
			if (arr.empty()) it = fmt::format_to(it, "(empty array)");
			else it = fmt::format_to(it, "(array) {:?}", fmt::join(arr, " "));
			break;
		}
		case NULLABLE: it = fmt::format_to(it, "(null)"); break;
		case DOUBLE:
			it = fmt::format_to(it, "(double) {}", value.as_double());
			break;
		default: std::unreachable();
		}
		break;
	case 'e':
	case 0:
		switch (value.type()) {
		case SIMPLE_STRING:
			it = fmt::format_to(it, "+{}\r\n", value.as_string());
			break;
		case SIMPLE_ERROR:
			it = fmt::format_to(it, "-{}\r\n", value.as_string());
			break;
		case INTEGEER:
			it = fmt::format_to(it, ":{}\r\n", value.as_integer());
			break;
		case BULK_STRING: {
			const auto &str = value.as_string();
			it = fmt::format_to(it, "${}\r\n{}\r\n", str.size(), str);
			break;
		}
		case ARRAY: {
			const auto &arr = value.as_array();
			it              = fmt::format_to(it,
                                "*{}\r\n{:e}",
                                arr.size(),
                                fmt::join(arr, ""));
			break;
		}
		case NULLABLE: it = format_to(it, "_\r\n"); break;
		case DOUBLE:
			it = fmt::format_to(it, ",{}\r\n", value.as_double());
			break;
		default: std::unreachable();
		}
		break;
	default: std::unreachable();
	}
	return it;
}
