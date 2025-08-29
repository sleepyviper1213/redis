#include "core/resp/parser.hpp"

#include "core/error.hpp"
#include "core/resp/value.hpp"

#include <fast_float/fast_float.h>

#include <string_view>
#include <utility>

namespace redis {
namespace resp {
std::expected<Value, Parser::RespError> Parser::parse(std::string_view data) {
	size_t pos             = 0;
	size_t recursive_depth = 0;
	return Parser::parse_recursive(data, recursive_depth, pos);
}

std::expected<Value, Parser::RespError>
Parser::parse_recursive(std::string_view data, size_t &recursive_depth,
						size_t &pos) {
	using enum RespError;
	if (pos >= data.size()) FAILED(UNEXPECTED_EOF);
	if (recursive_depth == DEPTH_LIMIT) FAILED(RECURSIVE_DEPTH_EXCEEDED);
	recursive_depth++;

	// Read a line
	const auto line_end = data.find("\r\n", pos);
	if (line_end == std::string_view::npos) FAILED(MISSING_CRLF);

	std::string_view line = data.substr(pos, line_end - pos); // exclude CRLF
	pos                   = line_end + 2; // move past "\r\n"

	if (line.empty()) FAILED(EMPTY_LINE);

	const char prefix        = line[0];
	std::string_view payload = line.substr(1);

	switch (prefix) {
	case '+': return Value::from_simple_string(std::string(payload));
	case '-': return Value::from_simple_error(std::string(payload));
	case ':': {
		auto i = TRY_FROM(parse_integer(payload), RespError);
		return Value::from_integer(i);
	}
	case '$': {
		const auto len = TRY_FROM(parse_integer(payload), RespError);
		if (len == -1) return Value::from_null();
		if (len < -1) FAILED(INVALID_BULK_LENGTH);

		if (pos + len + CRLF.size() > data.size())
			FAILED(TRUNCATED_BULK_STRING);

		std::string_view bulk_data = data.substr(pos, len);
		if (!is_crlf(data.substr(pos + len, CRLF.size()))) FAILED(MISSING_CRLF);

		pos += len + CRLF.size(); // skip data + CRLF
		return Value::from_bulk_string(std::string(bulk_data));
	}
	case '*': {
		const auto len = TRY_FROM(parse_integer(payload), RespError);
		if (len == -1) return Value::from_null();
		if (len < -1) FAILED(INVALID_ARRAY_LENGTH);

		std::vector<Value> arr;
		arr.reserve(len);

		for (int i = 0; i < len; ++i) {
			auto v = TRY(parse_recursive(data, recursive_depth, pos));
			arr.push_back(std::move(v));
		}
		recursive_depth--;

		return Value::from_array(std::move(arr));
	}
	case '_':
		if (!payload.empty()) FAILED(SYNTAX_ERROR);
		return Value::from_null();
	case ',': {
		auto number = TRY_FROM(parse_double(payload), RespError);
		return Value::from_double(number);
	}
	default: FAILED(NOT_SUPPORTED);
	}
}

bool Parser::is_crlf(std::string_view s) { return s == CRLF; }

std::expected<int64_t, std::errc> Parser::parse_integer(std::string_view s) {
	int64_t val{};
	auto [ptr, ec] = fast_float::from_chars(s.begin(), s.end(), val);
	if (ec != std::errc() || ptr != s.end()) FAILED(ec);
	return val;
}

std::expected<double, std::errc> Parser::parse_double(std::string_view s) {
	double val{};
	auto [ptr, ec] = fast_float::from_chars(s.begin(), s.end(), val);
	if (ec != std::errc() || ptr != s.end()) FAILED(ec);
	return val;
}

} // namespace resp

resp::Parser::RespError
From<std::errc, resp::Parser::RespError>::convert(std::errc ec) noexcept {
	switch (ec) {
	case std::errc::result_out_of_range:
	case std::errc::invalid_argument:
		return resp::Parser::RespError::PARSE_NUMBER;
	default: std::unreachable();
	}
}
} // namespace redis
