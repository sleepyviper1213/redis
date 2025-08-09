#include "value.hpp"

#include <fmt/ranges.h>

#include <cassert>
#include <utility>
#include <variant>
#include <vector>

namespace resp {
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

// std::string format_as(const Value &value) {
//}
int64_t Value::getInteger() const {
	assert(type_ == Type::Integer);
	return std::get<int64_t>(data_);
}

const Value::Array &Value::getArray() const {
	assert(type_ == Type::Array);
	return std::get<Array>(data_);
}

const std::string &Value::getString() const {
	assert(type_ == Type::SimpleString || type_ == Type::BulkString ||
		   type_ == Type::SimpleError);
	return std::get<std::string>(data_);
}

bool Value::is_null() const { return type_ == Type::Null; }

bool Value::is_error() const {
	return type_ == Type::SimpleError &&
		   std::holds_alternative<std::string>(data_);
}

bool Value::is_bulk_string() const {
	return type_ == Type::BulkString &&
		   std::holds_alternative<std::string>(data_);
}

bool Value::is_simple_string() const {
	return type_ == Type::SimpleString &&
		   std::holds_alternative<std::string>(data_);
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

double Value::getDouble() const { return std::get<double>(data_); }

bool Value::is_integer() const {
	return type_ == Type::Integer && std::holds_alternative<int64_t>(data_);
}

bool Value::is_double() const {
	return type_ == Type::Double && std::holds_alternative<double>(data_);
}
} // namespace resp

auto fmt::formatter<resp::Value>::format(const resp::Value &value,
										 format_context &ctx) const
	-> format_context::iterator {
	using enum resp::Value::Type;

	auto it = ctx.out();
	if (!presentation || presentation == '?') {
		switch (value.type()) {
		case SimpleString:
			it = fmt::format_to(it, "{}", value.getString());
			break;
		case SimpleError:
			it = fmt::format_to(it, "SimpleError({:?})", value.getString());
			break;
		case Integer:
			it = fmt::format_to(it, "Integer({})", value.getInteger());
			break;
		case BulkString: {
			it = fmt::format_to(it, "{:?}", value.getString());
			break;
		}
		case Array: {
			const auto &arr = value.getArray();
			if (arr.empty()) it = fmt::format_to(it, "(Empty Array)");
			else it = fmt::format_to(it, "Array({})", fmt::join(arr, ", "));
			break;
		}
		case Null: it = fmt::format_to(it, "(Null)"); break;
		case Double:
			it = fmt::format_to(it, "Double({})", value.getDouble());
			break;
		default: std::unreachable();
		}
	} else if (presentation == 'e') {
		switch (value.type()) {
		case SimpleString:
			it = fmt::format_to(it, "+{}\r\n", value.getString());
			break;
		case SimpleError:
			it = fmt::format_to(it, "-{}\r\n", value.getString());
			break;
		case Integer:
			it = fmt::format_to(it, ":{}\r\n", value.getInteger());
			break;
		case BulkString: {
			const auto &str = value.getString();
			it = fmt::format_to(it, "${}\r\n{}\r\n", str.size(), str);
			break;
		}
		case Array: {
			const auto &arr = value.getArray();
			it              = fmt::format_to(it,
                                "*{}\r\n{:e}",
                                arr.size(),
                                fmt::join(arr, ""));
			break;
		}
		case Null: it = format_to(it, "_\r\n"); break;
		case Double:
			it = fmt::format_to(it, ",{}\r\n", value.getDouble());
			break;
		default: std::unreachable();
		}
	}
	return it;
}
