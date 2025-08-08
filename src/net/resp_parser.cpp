#include "resp_parser.hpp"

#include <fmt/ranges.h>
#include <fmt/std.h>

#include <charconv>

namespace redis {

std::optional<std::pair<RespValue, size_t>>
RespParser::parse(std::string_view data) {
	if (data.empty()) return std::nullopt;

	switch (data[0]) {
	case '+': return parse_simple_string(data);
	case '-': return parse_error(data);
	case ':': return parse_integer(data);
	case '$': return parse_bulk_string(data);
	case '*': return parse_array(data);
	default: return std::nullopt;
	}
}

std::optional<std::pair<RespValue, size_t>>
RespParser::parse_simple_string(std::string_view data) {
	return parse_until_crlf(data, 1, RespValue::Type::SimpleString);
}

std::optional<std::pair<RespValue, size_t>>
RespParser::parse_error(std::string_view data) {
	return parse_until_crlf(data, 1, RespValue::Type::Error);
}

std::optional<std::pair<RespValue, size_t>>
RespParser::parse_integer(std::string_view data) {
	auto result = parse_until_crlf(data, 1, RespValue::Type::Integer);
	if (!result) return std::nullopt;

	long long value;
	auto str = std::get<std::string>(result->first.data);
	auto [ptr, ec] =
		std::from_chars(str.data(), str.data() + str.size(), value);

	if (ec != std::errc() || ptr != str.data() + str.size())
		return std::nullopt;

	result->first.data = value;
	return result;
}

std::optional<std::pair<RespValue, size_t>>
RespParser::parse_bulk_string(std::string_view data) {
	// Parse length
	size_t pos   = 1;
	auto len_end = data.find("\r\n", pos);
	if (len_end == std::string_view::npos) return std::nullopt;

	long long length;
	auto len_str   = data.substr(pos, len_end - pos);
	auto [ptr, ec] = std::from_chars(len_str.data(),
									 len_str.data() + len_str.size(),
									 length);

	if (ec != std::errc() || length < -1) return std::nullopt;
	if (length == -1) { // Null bulk string
		return std::make_pair(RespValue::from_simple_string("dummy"),
							  len_end + 2);
	}

	// Check if we have complete string + CRLF
	size_t bulk_start = len_end + 2;
	size_t bulk_end   = bulk_start + length;
	if (data.size() < bulk_end + 2) return std::nullopt;
	if (data.substr(bulk_end, 2) != "\r\n") return std::nullopt;

	return std::make_pair(RespValue::from_bulk_string(
							  std::string(data.substr(bulk_start, length))),
						  bulk_end + 2);
}

std::optional<std::pair<RespValue, size_t>>
RespParser::parse_array(std::string_view data) {
	// Parse array size
	size_t pos   = 1;
	auto len_end = data.find("\r\n", pos);
	if (len_end == std::string_view::npos) return std::nullopt;

	long long size;
	auto len_str = data.substr(pos, len_end - pos);
	auto [ptr, ec] =
		std::from_chars(len_str.data(), len_str.data() + len_str.size(), size);

	if (ec != std::errc() || size < -1) return std::nullopt;
	if (size == -1) { // Null array
		return std::make_pair(RespValue::from_simple_string("dummy"),
							  len_end + 2);
	}

	// Parse elements
	std::vector<RespValue> elements;
	pos = len_end + 2;
	for (long long i = 0; i < size; ++i) {
		auto element = parse(data.substr(pos));
		if (!element) return std::nullopt;
		elements.push_back(element->first);
		pos += element->second;
	}

	return std::make_pair(RespValue::from_array(std::move(elements)), pos);
}

std::optional<std::pair<RespValue, size_t>>
RespParser::parse_until_crlf(std::string_view data, size_t start,
							 RespValue::Type type) {
	auto end = data.find("\r\n", start);
	if (end == std::string_view::npos) return std::nullopt;

	return std::make_pair(
		RespValue{type, std::string(data.substr(start, end - start))},
		end + 2);
}

} // namespace redis
