#pragma once

#include "command_type.hpp"
#include "error.hpp"

#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

#include <string>
#include <vector>

class Command {
public:
	Command(CommandType type, std::vector<std::string> args);

	// TODO: default value needed as required by cereal's load from binary
	// functionality
	Command();

	static ErrorOr<Command> fromString(const std::string &line);

	[[nodiscard]] bool isModifiableCommand() const;

	[[nodiscard]] bool isFlushDatabase() const;

	CommandType type() const;

	[[nodiscard]] std::vector<std::string> args() const;

	template <class Archive>
	void serialize(Archive &ar) {
		ar(type_, args_);
	}

private:
	CommandType type_;
	std::vector<std::string> args_;
};

std::string format_as(const Command &command);
