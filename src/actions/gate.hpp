#pragma once
#include <string>

#include "actions.hpp"
#include "error.hpp"
#include "primitive/ret.hpp"

/**
 * \brief High-level interface for parsing and executing commands.
 */
class Gate {
public:
	/**
	 * \brief Parses a raw command string and executes it.
	 * \param cmd Raw command string.
	 * \return Result of the command as a Ret variant.
	 */
	ErrorOr<Ret> parseAndExecute(const std::string &cmd);

private:
	Db db_;             ///< In-memory database instance.
	Snapshot snapshot_; ///< Snapshot manager for persistence.
};
