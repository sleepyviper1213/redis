#pragma once
#include <cstdint>
#include <expected>
#include <string_view>
#include <system_error>

namespace redis {

/**
 * \brief Parse a signed 64-bit integer from decimal ASCII.
 *
 * The whole of \p s must be the number: no spaces, no '+', no trailing
 * characters.
 * \return The value; invalid_argument for malformed input (empty included),
 * result_out_of_range for a value outside int64.
 */
[[nodiscard]] std::expected<int64_t, std::errc>
parse_integer(std::string_view s);

/**
 * \brief Parse a double from ASCII; the whole of \p s must be the number.
 * \return The value; an error code for malformed or out-of-range input.
 */
[[nodiscard]] std::expected<double, std::errc> parse_double(std::string_view s);

} // namespace redis
