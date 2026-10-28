#pragma once
#include "core/resp/parser.hpp"
#include "core/resp/value.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>

namespace redis::resp {

/**
 * \brief Longest inline command accepted (same limit as Redis'
 * PROTO_INLINE_MAX_SIZE). Without one, a client that never sends a newline
 * could make the server buffer forever while looking for the end of a line.
 */
inline constexpr size_t MAX_INLINE_LENGTH = 64 * 1024;

/**
 * \brief Cut one client request out of \p data, starting at \p pos.
 *
 * Clients speak two request formats, told apart by the first byte:
 * - a RESP array of bulk strings (`*2\r\n$3\r\nGET\r\n$1\r\nk\r\n`), which is
 *   what client libraries send;
 * - an inline command (`GET k\r\n`), which is what telnet, nc and
 *   redis-benchmark's *_INLINE tests send. Arguments are split on spaces and
 *   tabs.
 *
 * \return The request as an array of bulk strings, advancing \p pos past it;
 *         std::nullopt for a blank inline line, which is consumed and ignored
 *         like Redis does; or an error. Parser::is_incomplete() tells whether
 *         the error just means more bytes are needed.
 */
[[nodiscard]] std::expected<std::optional<Value>, Parser::RespError>
parse_request(std::string_view data, size_t &pos);

} // namespace redis::resp
