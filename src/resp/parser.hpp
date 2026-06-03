#pragma once
#include "core/convert/from.hpp"
#include "core/error/error_enum_formatter.hpp"
#include "value.hpp"

#include <string_view>
#include <vector>

namespace redis {

namespace resp {
/**
 * \brief Parser for RESP-formatted data.
 */
class Parser {
public:
	enum class RespError {
		UNEXPECTED_EOF,
		MISSING_CRLF,
		EMPTY_LINE,
		INVALID_BULK_LENGTH,
		TRUNCATED_BULK_STRING,
		INVALID_ARRAY_LENGTH,
		PARSE_NUMBER,
		NOT_SUPPORTED,
		SYNTAX_ERROR
	};

	/**
	 * \brief Parse a single RESP value from the provided buffer.
	 * \param data The input buffer containing RESP-encoded bytes.
	 * \return The parsed Value on success; an error on failure.
	 */
	static std::expected<Value, RespError> parse(std::string_view data);

	/**
	 *  \brief Check if the given view starts with a CRLF sequence.
	 *  \param s The string view to test.
	 *  \return true if \p s begins with "\\r\\n"; false otherwise.
	 */
	[[nodiscard]] static bool is_crlf(std::string_view s);

	struct ParseResult {
		std::optional<std::vector<std::string>> cmd;
		std::size_t consumed;
	};

	static ParseResult try_parse(std::string_view input) {
		if (input.empty()) return {std::nullopt, 0};

		if (input[0] != '*') {
			// RESP array must start with '*'
			return {std::nullopt, 0};
		}

		// find CRLF after "*N\r\n"
		auto crlf = input.find("\r\n");
		if (crlf == std::string_view::npos)
			return {std::nullopt, 0}; // incomplete

		int nargs       = std::stoi(std::string(input.substr(1, crlf - 1)));
		std::size_t pos = crlf + 2;

		std::vector<std::string> args;
		args.reserve(nargs);

		for (int i = 0; i < nargs; i++) {
			if (pos >= input.size() || input[pos] != '$')
				return {std::nullopt, 0}; // incomplete or invalid

			auto crlf_len = input.find("\r\n", pos);
			if (crlf_len == std::string_view::npos) return {std::nullopt, 0};

			int len = std::stoi(
				std::string(input.substr(pos + 1, crlf_len - pos - 1)));
			pos = crlf_len + 2;

			if (pos + len + 2 > input.size())
				return {std::nullopt, 0}; // incomplete bulk string

			args.emplace_back(input.substr(pos, len));
			pos += len;

			if (pos + 2 > input.size() || input[pos] != '\r' ||
				input[pos + 1] != '\n')
				return {std::nullopt, 0}; // incomplete
			pos += 2;
		}

		return {std::move(args), pos};
	}

	/**
	 * \brief Parse a signed 64-bit integer from a decimal ASCII
	 * representation.
	 * \param s The ASCII slice containing the integer (no leading/trailing
	 * CRLF).
	 * \return The parsed integer on success; an error on malformed input or
	 * out-of-range value.
	 */
	static std::expected<int64_t, std::errc> parse_integer(std::string_view s);

	/**
	 * \brief Parse a double-precision floating-point number from ASCII.
	 * \param s The ASCII slice containing the number (no leading/trailing
	 * CRLF).
	 * \return The parsed double on success; an error on malformed input.
	 */
	static std::expected<double, std::errc> parse_double(std::string_view s);

	static std::expected<Value, RespError>
	parse_recursive(std::string_view data, size_t &pos);

private:
	/**
	 * \brief Carriage-return and line-feed delimiter used by RESP.
	 */
	static constexpr std::string_view CRLF = "\r\n";
};

} // namespace resp

template <>
struct From<std::errc, resp::Parser::RespError> {
	static resp::Parser::RespError convert(std::errc ec) noexcept;
};

} // namespace redis

ENUM_DEBUG_FORMATTER(redis::resp::Parser::RespError);
