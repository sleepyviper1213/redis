#include "cmd.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <expected>
#include <utility>

Command::Command(CommandType type, std::string args)
	: type_(type), args_(std::move(args)) {}

std::string format_as(const Command &command) {
	return fmt::format("[Command] {} {}", command.type(), command.args());
}

bool Command::isModifiableCommand() const {
	// TODO
}

bool Command::isFlushDatabase() const { return type_ == CommandType::FLUSHDB; }

CommandType Command::type() const { return type_; }

const std::string &Command::args() const { return args_; }

ErrorOr<Command> Command::fromString(const std::string &line) {
	std::istringstream iss{line};
	std::string cmd;
	iss >> cmd;
	std::string args;
	std::getline(iss, args);
	if (iss.fail())
		return failed("String contains invalid arguments",
					  std::errc::invalid_argument);

	std::ranges::transform(args, args.begin(), [](unsigned char c) {
		return std::tolower(c);
	});

	auto type = TRY(ok_or(CommandTypeUtil::fromString(cmd),
						  "String contains invalid command",
						  std::errc::invalid_argument));


	return Command{type, std::move(args)};
}
