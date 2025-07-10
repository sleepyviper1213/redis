#include "cmd.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <utility>
#include <variant>

Command::Command(CommandType type, std::string args)
	: type_(type), args_(std::move(args)) {}

std::string format_as(const Command &command) {
	return fmt::format("[Command] {} {}", command.type(), command.args());
}

bool Command::isModifiableCommand() const {
	return std::holds_alternative<StringCommand>(type_) ||
		   std::holds_alternative<KeyspaceCommand>(type_);
}

bool Command::isFlushDatabase() const {
	return std::holds_alternative<KeyspaceCommand>(type_) &&
		   std::get<KeyspaceCommand>(type_) == KeyspaceCommand::FLUSHDB;
}

CommandType Command::type() const { return type_; }

const std::string &Command::args() const { return args_; }

std::expected<Command, ParseError>
Command::fromString(const std::string &line) {
	std::istringstream iss{line};
	std::string cmd;
	iss >> cmd;
	std::string args;
	std::getline(iss, args);
	if (iss.fail()) return std::unexpected(ParseError::ARGS_PARSE_FAILED);

	std::ranges::transform(args, args.begin(), [](unsigned char c) {
		return std::tolower(c);
	});

	auto type = CommandTypeUtil::fromString(cmd);
	if (!type.has_value()) return std::unexpected(ParseError::TYPE_FAILED);

	return Command{*type, args};
}
