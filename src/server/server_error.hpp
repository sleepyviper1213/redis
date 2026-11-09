#pragma once

#include "core/error/display_debug.hpp"
#include "net/net_error.hpp"
#include "persistence/persist_error.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace redis {

/// What ends RedisServer::run() badly. The kind says which stage, the source is
/// the error from that stage, and the wording is in format_as().
enum class ServerKind : std::uint8_t {
	LoadFailed,      ///< [source = PersistError]: the files cannot be read
	ListenFailed,    ///< [source = ListenError]: the port cannot be taken
	WorkerFailed,    ///< [detail]: an I/O thread died
	FinalSaveFailed, ///< [source = PersistError]: the last snapshot was not written
};

/**
 * \brief An error with the error that caused it: what anyhow calls context
 * plus a source. The source is kept as a value, not a sentence, so whoever
 * logs this decides how to show both.
 */
struct ServerError {
	ServerKind kind;
	std::variant<std::monostate, persistence::PersistError, ListenError> source{};
	/// Text from outside that the message quotes: what an exception said.
	std::string detail{};
};

/// `{}`: the sentence. `{:?}` / `{:#?}`: the kind, the source error in its own
/// Debug form, and the detail. @see DisplayDebugFormatter
[[nodiscard]] std::string display(const ServerError &error);
[[nodiscard]] std::string debug_repr(const ServerError &error, bool pretty);

} // namespace redis

template <>
struct fmt::formatter<redis::ServerError>
	: redis::DisplayDebugFormatter<redis::ServerError> {};
