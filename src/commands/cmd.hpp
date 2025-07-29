#pragma once

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <string>
#include <vector>

#include "command_type.hpp"
#include "error.hpp"
namespace redis {

/**
 * \brief Represents a parsed command and its arguments.
 */
	class Command {
	public:
		/**
         * \brief Constructs a Command with type and arguments.
         */
		Command(CommandType type, std::vector<std::string> args);

		/**
         * \brief Default constructor required by cereal.
         */
		Command();

		/**
         * \brief Parses a Command from a raw line.
         */
		static ErrorOr<Command> fromString(const std::string &line);

		/**
         * \brief Checks if command modifies database state.
         */
		[[nodiscard]] bool isModifiableCommand() const;

		/**
         * \brief Checks if command flushes the entire DB.
         */
		[[nodiscard]] bool isFlushDatabase() const;

		/**
         * \brief Returns the command type.
         */
		CommandType type() const;

		/**
         * \brief Returns the command arguments.
         */
		[[nodiscard]] const std::vector<std::string> &args() const;

		/**
         * \brief Serializes the command using cereal.
         */
		template<class Archive>
		void serialize(Archive &ar) {
			ar(type_, args_);
		}

	private:
		CommandType type_;              ///< The command's type.
		std::vector<std::string> args_; ///< The command's arguments.
	};

/**
 * \brief String formatter for Command.
 */
std::string format_as(const Command &command);
} // namespace redis
