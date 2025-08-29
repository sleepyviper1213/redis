#pragma once
#include "../../include/core.hpp"
#include "core/error.hpp"
#include "primitive/reply.hpp"

#include <string>

namespace redis {

/**
 * \brief High-level interface for parsing and executing commands.
 */
class Gate {
public:
	/**
	 * \brief Parses a raw command string and executes it.
	 * \param cmd Raw command string.
	 * \return Result of the command as a Reply variant.
	 */
	ErrorOr<Reply> parseAndExecute(const std::string &cmd);

private:
	Db db_;             ///< In-memory database instance.
	Snapshot snapshot_; ///< Snapshot manager for persistence.
};
} // namespace redis