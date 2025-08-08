#pragma once

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <string>
#include <variant>
#include <vector>

namespace redis {

struct RespValue {
	using Array = std::vector<RespValue>;

	// using NullT = std::nullptr_t;


	enum class Type { SimpleString, Error, Integer, BulkString, ArrayT, Null };

	Type type;

	std::variant<std::string, // SimpleString, Error, BulkString
				 long long,   // Integer
				 Array        // Array
				 // NullT        // Null
				 >
		data;

	// Helper creators
	static RespValue from_simple_string(std::string s);

	static RespValue from_error(std::string s);

	static RespValue from_integer(long long value);

	static RespValue from_bulk_string(std::string s);

	static RespValue from_array(std::vector<RespValue> arr);

	// static RespValue null() { return {Null, nullptr}; }
};

} // namespace redis

template <>
struct fmt::formatter<redis::RespValue> {
	char presentation = 0;

	// Parses format specifiers and stores them in the formatter.
	//
	// [ctx.begin(), ctx.end()) is a, possibly empty, character range that
	// contains a part of the format string starting from the format
	// specifications to be parsed, e.g. in
	//
	//   fmt::format_to(it"{:f} continued", ...);
	//
	// the range will contain "f} continued". The formatter should parse
	// specifiers until '}' or the end of the range. In this example the
	// formatter should parse the 'f' specifier and return an iterator
	// pointing to '}'.
	constexpr auto parse(format_parse_context &ctx)
		-> format_parse_context::iterator {
		auto it = ctx.begin(), end = ctx.end();
		if (it == end) return it;
		if (*it == '?' || *it == 's') presentation = *it++;

		// Check if reached the end of the range:
		if (*it != '}') throw format_error("invalid format");

		// Return an iterator past the end of the parsed range:
		return it;
	}

	// Formats value using the parsed format specification stored in this
	// formatter and writes the output to it.
	auto format(const redis::RespValue &value, format_context &ctx) const
		-> format_context::iterator {
		using enum redis::RespValue::Type;

		auto it = ctx.out();
		if (presentation == '?') {
			switch (value.type) {
			case SimpleString:
				it = fmt::format_to(it,
									"SimpleString({:?})",
									std::get<std::string>(value.data));
				break;
			case Error:
				it = fmt::format_to(it,
									"Error({:?})",
									std::get<std::string>(value.data));
				break;
			case Integer:
				it = fmt::format_to(it,
									"Integer({})",
									std::get<long long>(value.data));
				break;
			case BulkString: {
				const auto &str = std::get<std::string>(value.data);
				it              = fmt::format_to(it, "BulkString({:?})", str);
				break;
			}
			case ArrayT: {
				const auto &arr = std::get<redis::RespValue::Array>(value.data);
				it = fmt::format_to(it, "Array({:?})", fmt::join(arr, ", "));
				break;
			}
			case Null: it = fmt::format_to(it, "Null"); break;
			default: it = fmt::format_to(it, "Unknown");
			}
		} else if (!presentation || presentation == 's') {
			switch (value.type) {
			case SimpleString:
				it = fmt::format_to(it,
									"+{}\r\n",
									std::get<std::string>(value.data));
				break;
			case Error:
				it = fmt::format_to(it,
									"-Err {}\r\n",
									std::get<std::string>(value.data));
				break;
			case Integer:
				it = fmt::format_to(it,
									":{}\r\n",
									std::get<long long>(value.data));
				break;
			case BulkString: {
				auto &str = std::get<std::string>(value.data);
				it = fmt::format_to(it, "${}\r\n{}\r\n", str.size(), str);
				break;
			}
			case ArrayT: {
				const auto &arr = std::get<redis::RespValue::Array>(value.data);
				it              = fmt::format_to(it, "*{}\r\n", arr.size());
				for (const auto &item : arr)
					it = fmt::format_to(it, "{}", item);
				break;
			}
			case Null: it = format_to(it, "$-1\r\n"); break;
			default: it = fmt::format_to(it, "-ERR unknown type\r\n");
			}
		}
		return it;
	}
};
