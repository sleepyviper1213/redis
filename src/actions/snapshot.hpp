#pragma once

#include "commands/cmd.hpp"
#include "error/error_or.hpp"

#include <shared_mutex>
#include <vector>

class Db;

/**
 * \brief Manages snapshot creation and restoration for persistence.
 */
class Snapshot {
public:
	/**
	 * \brief Constructs snapshot from a list of commands.
	 */
	Snapshot(std::vector<Command> list);

	/**
	 * \brief Default constructor.
	 */
	Snapshot() = default;

	/**
	 * \brief Clears the internal command list.
	 */
	void clear();

	/**
	 * \brief Adds a new command to the snapshot.
	 */
	void addCommand(Command command);

	/**
	 * \brief Creates a snapshot from the current database state.
	 * \param db The source database.
	 */
	ErrorOr<void> createFrom(Db &db);

	/**
	 * \brief Restores the database from the saved snapshot.
	 * \return A restored `Db` instance.
	 */
	ErrorOr<Db> restoreSnapshot();

private:
	std::vector<Command> commands_; ///< Serialized command history.
	std::shared_mutex cmds_mtx_;    ///< Protects command list.
	std::shared_mutex file_mtx_;    ///< Protects file access.
};
