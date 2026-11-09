#include "net_error.hpp"

#include "server/log_channels.hpp"
#include "common/resp/parser.hpp"

#include <fmt/format.h>
#include <spdlog/logger.h>

namespace redis {
namespace {
/// What the system said, or else the text that stood in for it.
std::string reason(const ConnectionError &error) {
	return error.cause ? error.cause.message() : error.quoted;
}
} // namespace

std::string display(const ListenError &error) {
	switch (error.kind) {
	case ListenKind::InvalidHost:
		return fmt::format("invalid host '{}': {}",
						   error.quoted,
						   error.cause.message());
	case ListenKind::CannotListen:
		return fmt::format("cannot listen on {}:{}: {}",
						   error.quoted,
						   error.at,
						   error.cause.message());
	}
	return "cannot listen";
}

std::string display(const ConnectionError &error) {
	switch (error.kind) {
	case ConnectionKind::ReadFailed:
		return fmt::format("Read: {}", error.cause.message());
	case ConnectionKind::WriteFailed:
		return fmt::format("Write: {}", error.cause.message());
	case ConnectionKind::ConnectionFailed:
		return fmt::format("Connection: {}", reason(error));
	case ConnectionKind::SessionFailed:
		return error.quoted.empty()
				   ? std::string("Session failed")
				   : fmt::format("Session failed: {}", error.quoted);
	case ConnectionKind::AcceptFailed:
		return fmt::format("Accept failed: {}", reason(error));
	case ConnectionKind::AcceptLoopFailed:
		return fmt::format("Accept loop failed: {}", error.quoted);
	case ConnectionKind::Protocol:
		return fmt::format("Protocol error: {}",
						   static_cast<resp::Parser::RespError>(error.code));
	}
	return "connection error";
}

void log_error(const ConnectionError &error) {
	auto &logger = logging::logger_for(logging::channel::net);
	if (error.kind == ConnectionKind::AcceptLoopFailed)
		logger.critical("{}", error);
	else logger.error("{}", error);
}

} // namespace redis
