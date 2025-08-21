#pragma once
#include "error.hpp"
#include "value.hpp"

#include <string_view>

namespace resp {

/**
 * \brief Parser for RESP-formatted data.
 */
class Parser {
public:
	/**
	 * \brief Parse a single RESP value from the provided buffer.
	 * \param data The input buffer containing RESP-encoded bytes.
	 * \return The parsed Value on success; an error on failure.
	 */
	static ErrorOr<Value> parse(std::string_view data);

	/**
	 *  \brief Check if the given view starts with a CRLF sequence.
	 *  \param s The string view to test.
	 *  \return true if \p s begins with "\r\n"; false otherwise.
	 */
	[[nodiscard]] static bool is_crlf(std::string_view s);

	/**
	 * \brief Parse a signed 64-bit integer from a decimal ASCII
	 * representation.
	 * \param s The ASCII slice containing the integer (no leading/trailing
	 * CRLF).
	 * \return The parsed integer on success; an error on malformed input or
	 * out-of-range value.
	 */
	static ErrorOr<int64_t> parse_integer(std::string_view s);

	/**
	 * \brief Parse a double-precision floating-point number from ASCII.
	 * \param s The ASCII slice containing the number (no leading/trailing
	 * CRLF).
	 * \return The parsed double on success; an error on malformed input.
	 */
	static ErrorOr<double> parse_double(std::string_view s);

protected:
	/**
	 * \brief Internal recursive descent entry to parse a RESP value.
	 * \param data The full input buffer being parsed.
	 * \param pos Current read position within \p data; advanced as bytes are
	 * consumed.
	 * \return The parsed Value on success; an error on failure.
	 */
	static ErrorOr<Value> parse_recursive(std::string_view data, size_t &pos);

private:
	/**
	 * \brief Maximum allowed RESP payload size in bytes.
	 * \details Used to limit allocation/processing of bulk data.
	 */
	static constexpr std::int64_t RESP_MAX_SIZE = 512LL * 1024 * 1024;

	/**
	 * \brief Carriage-return and line-feed delimiter used by RESP.
	 */
	static constexpr std::string_view CRLF = "\r\n";
};
} // namespace resp
