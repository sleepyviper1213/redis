#include "request.hpp"

#include <fast_float/fast_float.h>

#include <algorithm>
#include <utility>

namespace redis::resp {
namespace {
using RespError = Parser::RespError;

std::expected<RequestKind, RespError>
parse_inline(std::string_view data, size_t &pos,
			 std::vector<std::string_view> &args) {
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

	const size_t before = args.size();
	size_t i            = 0;
	while (i < line.size()) {
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
		const size_t start = i;
		while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
		if (i > start) args.push_back(line.substr(start, i - start));
	}

	return args.size() == before ? RequestKind::Blank : RequestKind::Command;
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
// Returns false, leaving `pos` and `args` untouched, for anything else (other
// element types, null bulk strings, incomplete or malformed input), so the
// generic parser still decides every error and every edge case.
bool parse_bulk_array(std::string_view data, size_t &pos,
					  std::vector<std::string_view> &args) {
	size_t p      = pos + 1; // past '*'
	int64_t count = 0;
	if (!read_length(data, p, count) || count < 1 ||
		count > Parser::MAX_ARRAY_RESERVE)
		return false;

	const size_t before = args.size();
	const auto give_up  = [&] {
		args.resize(before);
		return false;
	};
	for (int64_t i = 0; i < count; ++i) {
		int64_t length = 0;
		if (p >= data.size() || data[p] != '$') return give_up();
		++p;
		if (!read_length(data, p, length) || length < 0 ||
			length > Parser::MAX_BULK_LENGTH)
			return give_up();
		const auto size = static_cast<size_t>(length);
		if (data.size() - p < size + 2 || data[p + size] != '\r' ||
			data[p + size + 1] != '\n')
			return give_up();
		args.push_back(data.substr(p, size));
		p += size + 2;
	}
	pos = p;
	return true;
}
} // namespace

std::expected<RequestKind, Parser::RespError>
parse_request(std::string_view data, size_t &pos,
			  std::vector<std::string_view> &args,
			  std::deque<std::string> &owned) {
	if (pos >= data.size()) return std::unexpected(RespError::UNEXPECTED_EOF);
	if (data[pos] != '*') return parse_inline(data, pos, args);

	if (parse_bulk_array(data, pos, args)) return RequestKind::Command;

	// The generic parser advances `pos` as it reads, even when it then fails
	// for lack of bytes; restore it so an error consumes nothing.
	const size_t start = pos;
	auto value         = Parser::parse_recursive(data, pos);
	if (!value) {
		pos = start;
		return std::unexpected(value.error());
	}

	// Not Value::is_string(): a null Value holds an empty string too.
	const auto is_argument = [](const Value &v) {
		return v.is_bulk_string() || v.is_simple_string();
	};
	const bool well_formed = value->is_array() && !value->as_array().empty() &&
							 std::ranges::all_of(value->as_array(), is_argument);
	if (!well_formed) return RequestKind::Malformed;
	for (const Value &arg : value->as_array())
		args.push_back(owned.emplace_back(arg.as_string()));
	return RequestKind::Command;
}

} // namespace redis::resp
