#include "handler.hpp"

#include "core/logging.hpp"
#include "core/utils/toupper.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::commands);
}
} // namespace

using resp::Value;

std::optional<int64_t> parse_int(std::string_view s) noexcept {
	int64_t value = 0;
	const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
	if (ec != std::errc() || ptr != s.data() + s.size() || s.empty())
		return std::nullopt;
	return value;
}

CommandHandler::CommandHandler() {
	for (const auto family : {string_commands(),
							  keyspace_commands(),
							  list_commands(),
							  set_commands(),
							  hash_commands(),
							  server_commands()})
		for (const CommandInfo &info : family)
			commands_.emplace(std::string(info.name), info);
}

std::string CommandHandler::execute(CommandContext &ctx,
									const Value &request) const {
	log().trace("Query: {:?}", request);

	const bool well_formed =
		request.is_array() && !request.as_array().empty() &&
		std::ranges::all_of(request.as_array(), &Value::is_string);
	if (!well_formed)
		return fmt::format("{:e}",
						   reply::error("ERR Protocol error: expected a "
										"non-empty array of bulk strings"));

	const Args args = request.as_array();
	const std::string name = toupper(args[0].as_string());

	Value response = Value::from_null();
	if (const auto it = commands_.find(name); it == commands_.end()) {
		response = reply::error(
			fmt::format("ERR unknown command '{}'", args[0].as_string()));
	} else {
		const CommandInfo &info = it->second;
		const auto argc         = static_cast<int>(args.size());
		const bool arity_ok =
			info.arity >= 0 ? argc == info.arity : argc >= -info.arity;
		if (arity_ok) {
			response = info.fn(ctx, args);
		} else {
			std::string lowered = name;
			std::ranges::transform(lowered,
								   lowered.begin(),
								   [](unsigned char c) {
									   return static_cast<char>(std::tolower(c));
								   });
			response = reply::error(fmt::format(
				"ERR wrong number of arguments for '{}' command", lowered));
		}
	}

	log().trace("Response: {:?}", response);
	return fmt::format("{:e}", response);
}

} // namespace redis
