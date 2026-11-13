#pragma once

#include "core/error/failure_format.hpp"

#include <cstdint>
#include <exception>
#include <string>

namespace redis {

/// What can go wrong putting the server on a port. The fields each kind fills
/// in (see Failure) are in brackets.
enum class ListenKind : std::uint8_t {
	InvalidHost,  ///< [quoted = the host, cause]
	CannotListen, ///< [quoted = the host, at = the port, cause]
};

using ListenError = Failure<ListenKind>;

[[nodiscard]] std::string display(const ListenError &error);

/// What can go wrong with a client's connection, or in the loop that accepts
/// them. [cause] is what the system said; [quoted] stands in for it when all
/// that is known is the text of an exception.
enum class ConnectionKind : std::uint8_t {
	ReadFailed,       ///< [cause or quoted]
	WriteFailed,      ///< [cause or quoted]
	ExecuteFailed,    ///< [cause or quoted]
	ConnectionFailed, ///< [cause or quoted]
	SessionFailed,    ///< [cause or quoted, or neither]
	AcceptFailed,     ///< [cause or quoted]
	AcceptLoopFailed, ///< [cause or quoted]
	Protocol,         ///< [code = the resp parse error]
};

using ConnectionError = Failure<ConnectionKind>;

/// The sentence for \p error: the log line, and (behind "ERR ") the reply to a
/// client that sent something that is not RESP.
[[nodiscard]] std::string display(const ConnectionError &error);

/**
 * \brief The ConnectionError of kind \p kind that \p error stands for: a
 * system error's code as the cause, another std::exception's text quoted,
 * neither for anything else.
 *
 * Failures reach the senders' error channels as exception_ptrs, and throwing
 * one again is the only way to read it: this is the one place that does, so
 * that every caller handles every kind of exception alike.
 */
[[nodiscard]] ConnectionError connection_error(ConnectionKind kind,
											   const std::exception_ptr &error);

/// Log \p error on the network channel: critical for a dead accept loop, error
/// for anything else.
void log_error(const ConnectionError &error);

} // namespace redis
