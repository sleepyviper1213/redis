#include "core/resp/request.hpp"

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
} // namespace

std::expected<std::optional<Value>, Parser::RespError>
parse_request(std::string_view data, size_t &pos) {
	if (pos >= data.size()) return std::unexpected(RespError::UNEXPECTED_EOF);
	if (data[pos] != '*') return parse_inline(data, pos);

	auto value = Parser::parse_recursive(data, pos);
	if (!value) return std::unexpected(value.error());
	return std::optional<Value>(std::move(*value));
}

} // namespace redis::resp
