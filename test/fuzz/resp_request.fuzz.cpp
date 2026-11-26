// Fuzzes resp::parse_request, the framing a session runs on every read
// (DX-86, #51).
//
// TC-307: the input is cut into requests as a session cuts its buffer, by
// calling parse_request until it fails. At each request:
// - success moves `pos` forward, within the input; Command appends arguments,
//   Blank and Malformed append none;
// - an error leaves `pos` and the arguments as they were;
// - parsing only a prefix of the rest (the cut chosen by the first byte) never
//   gives another request than the whole rest, and fails only when more bytes
//   are needed or with the error the whole rest fails with. That is what lets
//   a session wait for more bytes on an incomplete error (TC-177).

#include "common/resp/parser.hpp"
#include "common/resp/request.hpp"
#include "core/error/verify.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace {
using redis::resp::parse_request;
using redis::resp::Parser;
using redis::resp::RequestKind;
using RespError = Parser::RespError;

/// One request, copied out so it can be compared across buffers.
struct Request {
	RequestKind kind;
	size_t end; ///< `pos` after the request
	std::vector<std::string> args;

	bool operator==(const Request &) const = default;
};

std::expected<Request, RespError> parse_one(std::string_view data,
											size_t pos) {
	std::vector<std::string_view> args;
	std::deque<std::string> owned;
	const auto kind = parse_request(data, pos, args, owned);
	if (!kind) return std::unexpected(kind.error());
	return Request{*kind, pos, {args.begin(), args.end()}};
}

/// The bytes before \p cut, a fraction of the rest picked by \p selector.
size_t cut_point(size_t pos, size_t size, uint8_t selector) {
	return pos + ((size - pos) * selector / 256);
}

void check_prefix_agrees(std::string_view data, size_t pos, uint8_t selector,
						 const std::expected<Request, RespError> &whole) {
	const auto prefix =
		parse_one(data.substr(0, cut_point(pos, data.size(), selector)), pos);
	if (prefix) {
		REDIS_VERIFY(whole && *prefix == *whole,
					 "a prefix parsed to another request than the whole input");
		return;
	}
	REDIS_VERIFY(Parser::is_incomplete(prefix.error()) ||
					 (!whole && whole.error() == prefix.error()),
				 "a prefix failed with an error the whole input does not have");
}

/// Checks one call of parse_request at \p pos; returns false once it failed.
bool check_step(std::string_view data, size_t &pos, uint8_t selector,
				std::vector<std::string_view> &args,
				std::deque<std::string> &owned) {
	const size_t start     = pos;
	const size_t arg_count = args.size();
	const auto kind        = parse_request(data, pos, args, owned);

	check_prefix_agrees(data, start, selector, parse_one(data, start));

	if (!kind) {
		REDIS_VERIFY(pos == start, "an error consumed bytes");
		REDIS_VERIFY(args.size() == arg_count, "an error appended arguments");
		return false;
	}
	REDIS_VERIFY(pos > start && pos <= data.size(),
				 "a request did not advance within the input");
	if (*kind == RequestKind::Command)
		REDIS_VERIFY(args.size() > arg_count, "a command has no arguments");
	else
		REDIS_VERIFY(args.size() == arg_count,
					 "a blank or malformed request appended arguments");
	return true;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size == 0) return 0;
	const std::string_view input(reinterpret_cast<const char *>(data), size);
	const uint8_t selector = data[0];

	// Kept across requests, as a session keeps them for a whole batch.
	std::vector<std::string_view> args;
	std::deque<std::string> owned;
	size_t pos = 0;
	while (check_step(input, pos, selector, args, owned)) {}
	return 0;
}
