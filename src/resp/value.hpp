#pragma once

#include <fmt/base.h>

#include "error.hpp"

#include <string>
#include <span>
#include <variant>
#include <vector>

namespace redis::resp {
/**
 * \brief Internal low-level redis value enum.
 */
class [[nodiscard]] Value {
public:
	using Array = std::vector<Value>;

	/**
	 * \brief Enumerates RESP value kinds.
	 *
	 * Notes:
	 * - RESP2 core types: SimpleString, SimpleError, Integer, BulkString,
	 * Array, Null
	 * - RESP3 extends with typed Null, Boolean, Double, BulkError, etc.
	 */
	enum class [[nodiscard]] Type {
		// RESP2
		/** Simple string reply, e.g. +OK\r\n */
		SimpleString,
		/** Simple error reply, e.g. -ERR message\r\n */
		SimpleError,
		/** Integer reply, e.g. :1000\r\n */
		Integer,
		/** Bulk string reply, e.g. $3\r\nfoo\r\n */
		BulkString,
		// RESP3
		/** Array reply, e.g. *2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n */
		Array,
		/** Null reply, e.g. $-1\r\n or *-1\r\n depending on context */
		Null,
		/** Boolean reply (RESP3), e.g. #t\r\n or #f\r\n */
		Boolean,
		/** Double reply (RESP3), e.g. ,3.1415\r\n */
		Double,
		/** Bulk error reply (RESP3), e.g. !<len>\r\n<msg>\r\n */
		BulkError,
	};

	/**
	 * \name Factory helpers
	 * \brief Create Value instances for specific RESP types.
	 * @{
	 */

	/** \brief Create a simple string value. */
	static Value from_simple_string(std::string s);

	/** \brief Create a simple error value. */
	static Value from_simple_error(std::string s);

	/** \brief Create an integer value. */
	static Value from_integer(int64_t value);

	/** \brief Create a bulk string value. */
	static Value from_bulk_string(std::string s);

	/** \brief Create an array value by taking ownership of elements. */
	static Value from_array(Array list);

	/** \brief Create an array value from a span of strings (converted to bulk
	 * strings). */
	static Value from_raw_array(std::span<const std::string> list);

	/** \brief Create a RESP null value. */
	static Value from_null();

	/** \brief Create a RESP3 double value. */
	static Value from_double(double d);
	/** @} */

	/** \brief Construct an empty value with explicit type; for internal use. */
	Value(Type type);

	/**
	 * \brief Construct a value providing both the type tag and the payload.
	 * \tparam T Payload type compatible with the active alternative.
	 */
	template <typename T>
	Value(Type type, T t) : type_(type), data_(std::move(t)) {}

	/**
	 * \name Accessors
	 * \brief Typed getters; call only when the corresponding is_*() is true.
	 * @{
	 */
	/**
	 * \return The integer payload.
	 * \note is_integer().
	 */
	[[nodiscard]] int64_t as_integer() const;

	/** \return The array payload.
	 * \note is_array(). */
	[[nodiscard]] const Array &as_array() const;

	/** \return The string payload (simple, error, or bulk).
	 * \note
	 * is_simple_string(), is_bulk_string(), or is_error().
	 */
	[[nodiscard]] const std::string &as_string() const;

	/** \return The double payload.
	 * \note is_double().
	 */
	[[nodiscard]] double as_double() const;
	/** @} */

	/** \return The current value type tag. */
	Type type() const;

	/**
	 * \name Type predicates
	 * \brief Safe queries for the active type.
	 * @{
	 */
	/** \brief Returns true if the value is Null; false otherwise. */
	[[nodiscard]] bool is_null() const;

	/** \brief Returns true if the value is a simple or bulk error; false
	 * otherwise. */
	[[nodiscard]] bool is_error() const;

	/** \brief Returns true if the value is an array; false otherwise. */
	[[nodiscard]] bool is_array() const;

	/** \brief Returns true if the value is a string; false otherwise. */
	[[nodiscard]] bool is_string() const;

	/** \brief Returns true if the value is a bulk string; false otherwise. */
	[[nodiscard]] bool is_bulk_string() const;

	/** \brief Returns true if the value is a simple string; false otherwise. */
	[[nodiscard]] bool is_simple_string() const;

	/** \brief Returns true if the value is an integer; false otherwise. */
	[[nodiscard]] bool is_integer() const;

	/** \brief Returns true if the value is a double; false otherwise. */
	[[nodiscard]] bool is_double() const;
	/** @} */

	/**
	 * \return The integer payload.
	 * \note is_integer().
	 */
	std::expected<int64_t, std::errc> try_as_integer() const;

private:
	/// Active type tag.
	Type type_;

	/// Payload storage:
	/// - std::string: SimpleString, SimpleError, BulkString
	/// - int64_t: Integer
	/// - Array: Array
	/// - double: Double
	std::variant<std::string, // SimpleString, Error, BulkString
				 int64_t,     // Integer
				 Array,       // Array
				 double>
		data_;
};

} // namespace redis::resp

/**
 * \brief Formatter specialization for resp::Value with fmtlib.
 *
 * Supported presentation types:
 * - '?' debug format
 * - 'e' RESP-encoded format
 * - none: same as 'e'
 */
template <>
struct fmt::formatter<redis::resp::Value> {
	/// Selected presentation specifier ('?' or 'e');
	char presentation = 0;

	/**
	 * \brief Parse format specifier.
	 * \param ctx Parsing context provided by fmt.
	 * \return Iterator to the end of the parsed range.
	 */
	constexpr auto parse(format_parse_context &ctx)
		-> format_parse_context::iterator {
		auto it = ctx.begin(), end = ctx.end();
		if (it == end) return it;
		if (*it == '?' || *it == 'e') presentation = *it++;

		if (*it != '}') report_error("invalid format specifier");

		return it;
	}

	/**
	 * \brief Format a resp::Value according to the selected presentation.
	 * \param value The value to format.
	 * \param ctx   The formatting context.
	 * \return Iterator to the end of the formatted output.
	 */
	auto format(const redis::resp::Value &value, format_context &ctx) const
		-> format_context::iterator;
};
