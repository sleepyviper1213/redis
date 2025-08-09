#include "resp_parser.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace resp {
ErrorOr<Value> Parser::parse(std::string_view data) {
	if (pos_ >= data.size())
		return failed("unexpected EOF", std::errc::result_out_of_range);

	// Read a line
	auto line_end = data.find('\n', pos_);
	if (line_end == std::string_view::npos)
		return failed("missing LF", std::errc::invalid_argument);

	if (line_end < 1 || data[line_end - 1] != '\r')
		return failed("invalid CRLF", std::errc::invalid_argument);

	std::string_view line =
		data.substr(pos_, line_end - pos_ - 1); // exclude CRLF
	pos_ = line_end + 1;                        // move past "\r\n"

	if (line.empty()) return failed("empty line", std::errc::invalid_argument);

	char prefix              = line[0];
	std::string_view payload = line.substr(1);

	switch (prefix) {
	case '+': return Value::from_simple_string(std::string(payload));
	case '-': return Value::from_simple_error(std::string(payload));
	case ':': {
		auto i = TRY(parse_integer(payload));
		return Value::from_integer(i);
	}
	case '$': {
		auto len = TRY(parse_integer(payload));
		if (len == -1) return Value::from_null();
		if (len < -1 || len >= RESP_MAX_SIZE)
			return failed("invalid bulk length",
						  std::errc::result_out_of_range);

		if (pos_ + len + CRLF.size() > data.size())
			return failed("truncated bulk string",
						  std::errc::result_out_of_range);

		std::string_view bulk_data = data.substr(pos_, len);
		if (!is_crlf(data.substr(pos_ + len, CRLF.size())))
			return failed("invalid CRLF in bulk", std::errc::invalid_argument);

		pos_ += len + CRLF.size(); // skip data + CRLF
		return Value::from_bulk_string(std::string(bulk_data));
	}
	case '*': {
		auto len = TRY(parse_integer(payload));
		if (len == -1) return Value::from_null();
		if (len < -1 || len >= RESP_MAX_SIZE)
			return failed("invalid array length",
						  std::errc::result_out_of_range);

		std::vector<Value> arr;
		arr.reserve(len);
		for (int i = 0; i < len; ++i) {
			auto v = TRY(parse(data));
			arr.push_back(std::move(v));
		}
		return Value::from_array(std::move(arr));
	}
	case '_':
		if (!payload.empty())
			return failed("RESP3 NULL contains _ only",
						  std::errc::invalid_argument);
		return Value::from_null();
	case ',': {
		auto number = TRY(parse_double(payload));
		return Value::from_double(number);
	}
	default: return failed("invalid RESP type", std::errc::not_supported);
	}
}

bool Parser::is_crlf(std::string_view s) const { return s == CRLF; }

ErrorOr<int64_t> Parser::parse_integer(std::string_view s) {
	int64_t val{};
	auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
	if (ec != std::errc()) return failed("integer parse failed", ec);
	return val;
}

ErrorOr<double> Parser::parse_double(std::string_view s) {
	double val = std::strtod(s.data(), nullptr);
	return val;
}
} // namespace resp
