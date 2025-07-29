#include "cmd.hpp"

#include "commands/command_type.hpp"
#include "utils/split_by_space.hpp"

#include <fmt/ranges.h>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace redis {

	Command::Command(CommandType type, std::vector<std::string> args)
			: type_(type), args_(std::move(args)) {}

	Command::Command() : type_(CommandType::KEYS) {}

	bool Command::isModifiableCommand() const {
		return type_ == CommandType::DEL || type_ == CommandType::SADD ||
			   type_ == CommandType::SREM || type_ == CommandType::SSET ||
			   type_ == CommandType::FLUSHDB || type_ == CommandType::LPUSH ||
			   type_ == CommandType::LPOP || type_ == CommandType::RPUSH ||
			   type_ == CommandType::RPOP;
	}

	bool Command::isFlushDatabase() const { return type_ == CommandType::FLUSHDB; }

	CommandType Command::type() const { return type_; }

	const std::vector<std::string> &Command::args() const { return args_; }

	ErrorOr<Command> Command::fromString(const std::string &line) {
		auto words = split_by_space(line);

		auto &cmd = words.front();

		// TODO: UTF-8 support
		std::ranges::transform(cmd, cmd.begin(), [](unsigned char c) {
			return std::tolower(c);
		});
		auto type = TRY(ok_or(CommandTypeUtil::fromString(cmd),
							  "[Command] Not a Redis command",
							  std::errc::invalid_argument));


		return Command{std::move(type), {words.begin() + 1, words.end()}};
	}

std::string format_as(const Command &command) {
	return fmt::format("[Command] {} {}", command.type(), command.args());
}} // namespace redis
