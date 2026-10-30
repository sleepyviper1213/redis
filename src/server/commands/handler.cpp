#include "handler.hpp"

#include "core/logging.hpp"
#include "core/utils/toupper.hpp"

#include <fast_float/fast_float.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::commands);
}
} // namespace

using resp::Value;

std::optional<int64_t> parse_int(std::string_view s) noexcept {
	int64_t value = 0;
	const auto [ptr, ec] =
		fast_float::from_chars(s.data(), s.data() + s.size(), value);
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

namespace {
// Longer than any command name: a longer one cannot match, so it is not
// looked up at all.
constexpr size_t MAX_COMMAND_NAME = 32;

// Upper-cases an ASCII command name into `buf`, so the lookup needs no
// allocation. Returns an empty view (which matches no command) when the name
// does not fit.
std::string_view upper_name(std::string_view name,
							std::array<char, MAX_COMMAND_NAME> &buf) {
	if (name.size() > buf.size()) return {};
	for (size_t i = 0; i < name.size(); ++i) {
		const char c = name[i];
		buf[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
	}
	return {buf.data(), name.size()};
}
} // namespace

std::string CommandHandler::execute(CommandContext &ctx,
									const Value &request) const {
	std::string out;
	execute(ctx, request, out);
	return out;
}

void CommandHandler::execute(CommandContext &ctx, const Value &request,
							 std::string &out) const {
	std::vector<std::string_view> args;
	// Not Value::is_string(): a null Value holds an empty string too.
	const bool well_formed =
		request.is_array() &&
		std::ranges::all_of(request.as_array(), [](const Value &arg) {
			return arg.is_bulk_string() || arg.is_simple_string();
		});
	if (well_formed)
		for (const Value &arg : request.as_array())
			args.push_back(arg.as_string());
	execute(ctx, args, out);
}

void CommandHandler::execute(CommandContext &ctx, Args args,
							 std::string &out) const {
	log().trace("Query: {::?}", args);

	if (args.empty()) {
		resp::encode_to(out,
						reply::error("ERR Protocol error: expected a "
									 "non-empty array of bulk strings"));
		return;
	}

	std::array<char, MAX_COMMAND_NAME> name_buf;

	Value response = Value::from_null();
	if (const auto it = commands_.find(upper_name(args[0], name_buf));
		it == commands_.end()) {
		response =
			reply::error(fmt::format("ERR unknown command '{}'", args[0]));
	} else {
		const CommandInfo &info = it->second;
		const auto argc         = static_cast<int>(args.size());
		const bool arity_ok =
			info.arity >= 0 ? argc == info.arity : argc >= -info.arity;
		if (arity_ok) {
			response = info.fn(ctx, args);
		} else {
			response = reply::error(
				fmt::format("ERR wrong number of arguments for '{}' command",
							tolower(std::string{info.name})));
		}
	}

	log().trace("Response: {:?}", response);
	resp::encode_to(out, response);
}

} // namespace redis
