#include "core/resp/request.hpp"

#include <fast_float/fast_float.h>

#include <string>
#include <utility>

namespace redis::resp {
namespace {
using RespError = Parser::RespError;

std::expected<std::optional<Value>, RespError>
parse_inline(std::string_view data, size_t &pos) {
	const auto newline = data.find('\n', pos);
	if (newline == std::string_view::npos) {
		if (data.size() - pos > MAX_INLINE_LENGTH)
			return std::unexpected(RespError::INLINE_TOO_LONG);
		return std::unexpected(RespError::UNEXPECTED_EOF);
	}
	if (newline - pos > MAX_INLINE_LENGTH)
		return std::unexpected(RespError::INLINE_TOO_LONG);

	std::string_view line = data.substr(pos, newline - pos);
	pos                   = newline + 1;
	if (line.ends_with('\r')) line.remove_suffix(1);

	Value::Array args;
	size_t i = 0;
	while (i < line.size()) {
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
		const size_t start = i;
		while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
		if (i > start)
			args.push_back(
				Value::from_bulk_string(std::string(line.substr(start, i - start))));
	}

	if (args.empty()) return std::nullopt;
	return Value::from_array(std::move(args));
}

// Reads a decimal followed by CRLF at `pos` into `out`, moving `pos` past it.
bool read_length(std::string_view data, size_t &pos, int64_t &out) {
	const char *first    = data.data() + pos;
	const char *last     = data.data() + data.size();
	const auto [end, ec] = fast_float::from_chars(first, last, out);
	if (ec != std::errc() || end == first || last - end < 2 || end[0] != '\r' ||
		end[1] != '\n')
		return false;
	pos = static_cast<size_t>(end - data.data()) + 2;
	return true;
}

// Fast path for the one shape clients really send: an array of non-null bulk
// strings. It reads lengths and slices payloads in place, without the
// per-element recursion, intermediate Values and line scans of the generic
// parser.
//
// Returns std::nullopt, leaving `pos` untouched, for anything else (other
// element types, null bulk strings, incomplete or malformed input), so the
// generic parser still decides every error and every edge case.
std::optional<Value> parse_bulk_array(std::string_view data, size_t &pos) {
	size_t p      = pos + 1; // past '*'
	int64_t count = 0;
	if (!read_length(data, p, count) || count < 1 ||
		count > Parser::MAX_ARRAY_RESERVE)
		return std::nullopt;

	Value::Array args;
	args.reserve(static_cast<size_t>(count));
	for (int64_t i = 0; i < count; ++i) {
		int64_t length = 0;
		if (p >= data.size() || data[p] != '$') return std::nullopt;
		++p;
		if (!read_length(data, p, length) || length < 0 ||
			length > Parser::MAX_BULK_LENGTH)
			return std::nullopt;
		const auto size = static_cast<size_t>(length);
		if (data.size() - p < size + 2 || data[p + size] != '\r' ||
			data[p + size + 1] != '\n')
			return std::nullopt;
		args.push_back(
			Value::from_bulk_string(std::string(data.substr(p, size))));
		p += size + 2;
	}
	pos = p;
	return Value::from_array(std::move(args));
}
} // namespace

std::expected<std::optional<Value>, Parser::RespError>
parse_request(std::string_view data, size_t &pos) {
	if (pos >= data.size()) return std::unexpected(RespError::UNEXPECTED_EOF);
	if (data[pos] != '*') return parse_inline(data, pos);

	if (auto fast = parse_bulk_array(data, pos)) return fast;

	auto value = Parser::parse_recursive(data, pos);
	if (!value) return std::unexpected(value.error());
	return std::optional<Value>(std::move(*value));
}

} // namespace redis::resp
