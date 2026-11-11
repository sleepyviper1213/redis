#include "parser.hpp"

#include "core/error.hpp"
#include "core/utils/parse_number.hpp"
#include "value.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace redis {
namespace resp {
std::expected<Value, Parser::RespError> Parser::parse(std::string_view data) {
	size_t pos = 0;
	return Parser::parse_recursive(data, pos);
}

std::expected<Value, Parser::RespError>
Parser::parse_recursive(std::string_view data, size_t &pos, int depth) {
	using enum RespError;
	if (depth > MAX_DEPTH) REDIS_FAILED(NESTING_TOO_DEEP);
	if (pos >= data.size()) REDIS_FAILED(UNEXPECTED_EOF);

	// Read a line
	const auto line_end = data.find("\r\n", pos);
	if (line_end == std::string_view::npos) REDIS_FAILED(UNEXPECTED_EOF);

	const std::string_view line =
		data.substr(pos, line_end - pos); // exclude CRLF
	pos                   = line_end + 2; // move past "\r\n"

	if (line.empty()) REDIS_FAILED(EMPTY_LINE);

	const char prefix        = line[0];
	const std::string_view payload = line.substr(1);

	switch (prefix) {
	case '+': return Value::from_simple_string(std::string(payload));
	case '-': return Value::from_simple_error(std::string(payload));
	case ':': {
		auto i = REDIS_TRY_FROM(parse_integer(payload), RespError);
		return Value::from_integer(i);
	}
	case '$': {
		const auto len = REDIS_TRY_FROM(parse_integer(payload), RespError);
		if (len == -1) return Value::from_null();
		if (len < -1 || len > MAX_BULK_LENGTH) REDIS_FAILED(INVALID_BULK_LENGTH);

		// len is in [0, MAX_BULK_LENGTH] here.
		const auto size = static_cast<size_t>(len);
		if (pos + size + CRLF.size() > data.size())
			REDIS_FAILED(TRUNCATED_BULK_STRING);

		const std::string_view bulk_data = data.substr(pos, size);
		if (!is_crlf(data.substr(pos + size, CRLF.size())))
			REDIS_FAILED(MISSING_CRLF);

		pos += size + CRLF.size(); // skip data + CRLF
		return Value::from_bulk_string(std::string(bulk_data));
	}
	case '*': {
		const auto len = REDIS_TRY_FROM(parse_integer(payload), RespError);
		if (len == -1) return Value::from_null();
		if (len < -1) REDIS_FAILED(INVALID_ARRAY_LENGTH);

		std::vector<Value> arr;
		arr.reserve(static_cast<size_t>(std::min(len, MAX_ARRAY_RESERVE)));

		for (int64_t i = 0; i < len; ++i) {
			auto v = REDIS_TRY(parse_recursive(data, pos, depth + 1));
			arr.push_back(std::move(v));
		}

		return Value::from_array(std::move(arr));
	}
	case '_':
		if (!payload.empty()) REDIS_FAILED(SYNTAX_ERROR);
		return Value::from_null();
	case ',': {
		auto number = REDIS_TRY_FROM(parse_double(payload), RespError);
		return Value::from_double(number);
	}
	default: REDIS_FAILED(NOT_SUPPORTED);
	}
}

bool Parser::is_incomplete(RespError err) noexcept {
	constexpr std::array INCOMPLETE{RespError::UNEXPECTED_EOF,
									RespError::TRUNCATED_BULK_STRING};
	return std::ranges::contains(INCOMPLETE, err);
}

bool Parser::is_crlf(std::string_view s) { return s == CRLF; }

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
