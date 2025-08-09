#pragma once

#include <fmt/format.h>

#include <span>
#include <string>
#include <variant>
#include <vector>

namespace resp {
class Value {
public:
	using Array = std::vector<Value>;

	/**
	 * Null Null bulk reply, $-1\r\n
	 * NullArray Null array reply, *-1\r\n
	 * String(String) For Simple Strings the first byte of the reply is “+”.
	 * Error(String) For Errors the first byte of the reply is “-”.
	 * Integer(i64) For Integers the first byte of the reply is “:”.
	 * Bulk(String) For Bulk Strings the first byte of the reply is “$”.
	 * BufBulk(Vec<u8>) For Bulk Strings the first byte of the reply is “$”.
	 * Array(vector<Value>) For Arrays the first byte of the reply is “*”.
	 */
	enum class [[nodiscard]] Type {
		// RESP2
		SimpleString,
		SimpleError,
		Integer,
		BulkString,
		// RESP3
		Array,
		Null,
		Boolean,
		Double,
		BulkError,
	};

	static Value from_simple_string(std::string s);

	static Value from_simple_error(std::string s);

	static Value from_integer(int64_t value);

	static Value from_bulk_string(std::string s);

	static Value from_array(Array list);

	static Value from_raw_array(std::span<const std::string> list);


	static Value from_null();

	static Value from_double(double d);

	Value(Type type);

	template <typename T>
	Value(Type type, T t) : type_(type), data_(std::move(t)) {}

	[[nodiscard]] int64_t getInteger() const;

	[[nodiscard]] const Array &getArray() const;

	[[nodiscard]] const std::string &getString() const;
	Type type() const;

	/**
	 * \brief Returns true if the value is a Null. Returns false otherwise.
	 */
	[[nodiscard]] bool is_null() const;

	/**
	 * \brief Returns true if the value is a Error. Returns false otherwise.
	 */
	[[nodiscard]] bool is_error() const;

	/**
	 * \brief Returns true if the value is a Error. Returns false otherwise.
	 */
	[[nodiscard]] bool is_array() const;
	/**
	 * \brief Returns true if the value is a Error. Returns false otherwise.
	 */
	[[nodiscard]] bool is_bulk_string() const;
	/**
	 * \brief Returns true if the value is a Error. Returns false otherwise.
	 */
	[[nodiscard]] bool is_simple_string() const;

private:
	Type type_;

	std::variant<std::string, // SimpleString, Error, BulkString
				 int64_t,     // Integer
				 Array,       // Array
				 double>
		data_;
};

} // namespace resp

template <>
struct fmt::formatter<resp::Value> {
	/*
	 * The available string presentation types are:
	 * ? debug format
	 * e resp encoded format
	 * none	The same as 'e'.
	 */
	char presentation = 0;

	constexpr auto parse(format_parse_context &ctx)
		-> format_parse_context::iterator {
		auto it = ctx.begin(), end = ctx.end();
		if (it == end) return it;
		if (*it == '?' || *it == 'e') presentation = *it++;

		// Check if reached the end of the range:
		if (*it != '}') report_error("invalid format specifier");

		// Return an iterator past the end of the parsed range:
		return it;
	}

	// Formats value using the parsed format specification stored in this
	// formatter and writes the output to it.
	auto format(const resp::Value &value, format_context &ctx) const
		-> format_context::iterator;
};
