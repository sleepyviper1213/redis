#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>

namespace redis {

/**
 * \brief An error as data: which thing went wrong, and the facts a message about
 * it needs, with no sentence built yet.
 *
 * Each module has its own \p Kind enum (what can go wrong there) and spells it
 * out in a `format_as(const Failure<Kind> &)` beside the enum. The failing site
 * only records what it knows; the wording lives in one place and is produced at
 * the edge that needs text: the log line, the RESP reply, the startup message.
 *
 * @code
 *   enum class PersistKind { CannotOpen, Locked, ... };
 *   using PersistError = Failure<PersistKind>;
 *
 *   REDIS_FAILED(PersistError{.kind = PersistKind::CannotOpen, .cause = ec, .path = p});
 *   ...
 *   log.error("{}", error);   // "cannot open /data/x: Permission denied"
 * @endcode
 *
 * Which fields a kind uses is part of that kind's wording, documented where the
 * enum is. An unused field stays empty. Errors are rare and cold, so the
 * fields are owned values (a path, a string) rather than views that must
 * outlive the failing call.
 */
template <typename Kind>
    requires std::is_scoped_enum_v<Kind>
struct Failure {
	Kind kind;
	/// What the system said, when a call failed.
	std::error_code cause{};
	/// The file or directory it concerned.
	std::filesystem::path path{};
	/// A position or a number: a byte offset, a port, a count.
	std::uint64_t at = 0;
	/// A second number the kind defines: what was expected beside the \p at that
	/// was found, or the value of an enumerator of another module's enum.
	std::int64_t code = 0;
	/// Text from outside that the message quotes: a name read from a file, a
	/// host given on the command line. Data, not a message.
	std::string quoted{};

	friend bool operator==(const Failure &, const Failure &) = default;
};

} // namespace redis
