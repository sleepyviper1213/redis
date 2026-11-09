#pragma once
#include "parser.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace redis::resp {

/**
 * \brief Longest inline command accepted (same limit as Redis'
 * PROTO_INLINE_MAX_SIZE). Without one, a client that never sends a newline
 * could make the server buffer forever while looking for the end of a line.
 */
inline constexpr size_t MAX_INLINE_LENGTH = 64 * 1024;

/// What parse_request() cut out of the input.
enum class RequestKind : uint8_t {
	/// A command; its arguments were appended.
	Command,
	/// A blank inline line, consumed and ignored like Redis does.
	Blank,
	/// Valid RESP but not a non-empty array of strings (`*0`, `*-1`, an
	/// integer or null element). Consumed with nothing appended; the command
	/// handler answers it with an error.
	Malformed,
};

/**
 * \brief Cut one client request out of \p data, starting at \p pos, and append
 * its arguments to \p args.
 *
 * Clients speak two request formats, told apart by the first byte:
 * - a RESP array of bulk strings (`*2\r\n$3\r\nGET\r\n$1\r\nk\r\n`), which is
 *   what client libraries send;
 * - an inline command (`GET k\r\n`), which is what telnet, nc and
 *   redis-benchmark's *_INLINE tests send. Arguments are split on spaces and
 *   tabs.
 *
 * Arguments are not copied: they are views into \p data, valid only while
 * \p data is neither changed nor freed. The rare request that only the generic
 * parser accepts (simple strings as elements, say) is decoded into \p owned,
 * which never moves its strings, and viewed there.
 *
 * \return What was cut, advancing \p pos past it; or an error, which leaves
 *         \p pos and \p args unchanged. Parser::is_incomplete() tells whether
 *         the error just means more bytes are needed.
 */
[[nodiscard]] std::expected<RequestKind, Parser::RespError>
parse_request(std::string_view data, size_t &pos,
			  std::vector<std::string_view> &args,
			  std::deque<std::string> &owned);

} // namespace redis::resp
