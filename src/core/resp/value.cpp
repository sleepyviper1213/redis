#include "core/error.hpp"
#include "core/resp.hpp"

#include <fmt/ranges.h>

#include <cassert>
#include <charconv>
#include <iterator>
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
	return TRY(Parser::parse_integer(as_string()));
}

const Value::Array &Value::as_array() const {
	assert(is_array());
	return std::get<Array>(data_);
}

const std::string &Value::as_string() const {
	assert(type_ == Type::SimpleString || type_ == Type::BulkString ||
		   type_ == Type::SimpleError);

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

void encode_to(std::string &out, const Value &value) {
	using enum Value::Type;

	char digits[24]; // fits any int64_t or size_t in decimal
	const auto append_number = [&](auto n) {
		const auto [end, ec] = std::to_chars(digits, digits + sizeof digits, n);
		out.append(digits, end);
	};

	switch (value.type()) {
	case SimpleString:
		out += '+';
		out += value.as_string();
		out += "\r\n";
		break;
	case SimpleError:
		out += '-';
		out += value.as_string();
		out += "\r\n";
		break;
	case Integer:
		out += ':';
		append_number(value.as_integer());
		out += "\r\n";
		break;
	case BulkString: {
		const auto &str = value.as_string();
		out += '$';
		append_number(str.size());
		out += "\r\n";
		out += str;
		out += "\r\n";
		break;
	}
	case Array: {
		const auto &arr = value.as_array();
		out += '*';
		append_number(arr.size());
		out += "\r\n";
		for (const auto &element : arr) encode_to(out, element);
		break;
	}
	// RESP2 null bulk string; see the formatter below.
	case Null: out += "$-1\r\n"; break;
	case Double:
		fmt::format_to(std::back_inserter(out), ",{}\r\n", value.as_double());
		break;
	default: std::unreachable();
	}
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
		case SimpleString:
			it = fmt::format_to(it, "+{}\r\n", value.as_string());
			break;
		case SimpleError:
			it = fmt::format_to(it, "-{}\r\n", value.as_string());
			break;
		case Integer:
			it = fmt::format_to(it, ":{}\r\n", value.as_integer());
			break;
		case BulkString: {
			const auto &str = value.as_string();
			it = fmt::format_to(it, "${}\r\n{}\r\n", str.size(), str);
			break;
		}
		case Array: {
			const auto &arr = value.as_array();
			it              = fmt::format_to(it,
                                "*{}\r\n{:e}",
                                arr.size(),
                                fmt::join(arr, ""));
			break;
		}
		// RESP2 null bulk string. RESP3's "_" is only legal after a client
		// negotiates protocol 3 with HELLO, which this server does not offer.
		case Null: it = fmt::format_to(it, "$-1\r\n"); break;
		case Double:
			it = fmt::format_to(it, ",{}\r\n", value.as_double());
			break;
		default: std::unreachable();
		}
		break;
	default: std::unreachable();
	}
	return it;
}
