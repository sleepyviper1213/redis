#pragma once

#include "command_type.hpp"
#include "error/parse_error.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/types/variant.hpp>
#include <fmt/base.h>

#include <expected>
#include <string>

// TODO: Move serialistion API to libcereal
class Command {
public:
	Command(CommandType type, std::string args);

	static std::expected<Command, ParseError>
	fromString(const std::string &line);

	[[nodiscard]] bool isModifiableCommand() const;

	[[nodiscard]] bool isFlushDatabase() const;

	[[nodiscard]] CommandType type() const;

	[[nodiscard]] const std::string &args() const;

	template <class Archive>
	void serialize(Archive &ar) {
		ar(type_, args_);
	}

private:
	CommandType type_;
	std::string args_;
};

std::string format_as(const Command &command);
