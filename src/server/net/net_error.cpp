#include "net_error.hpp"

#include "common/resp/parser.hpp"
#include "server/log_channels.hpp"

#include <boost/system/system_error.hpp>
#include <fmt/format.h>
#include <spdlog/logger.h>

#include <system_error>

namespace redis {
namespace {
/// What the system said, or else the text that stood in for it.
std::string reason(const ConnectionError &error) {
	if (error.cause) return error.cause.message();
	return error.quoted.empty() ? std::string("unknown error") : error.quoted;
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
		return fmt::format("Read: {}", reason(error));
	case ConnectionKind::WriteFailed:
		return fmt::format("Write: {}", reason(error));
	case ConnectionKind::ExecuteFailed:
		return fmt::format("Execute: {}", reason(error));
	case ConnectionKind::ConnectionFailed:
		return fmt::format("Connection: {}", reason(error));
	case ConnectionKind::SessionFailed:
		return !error.cause && error.quoted.empty()
				   ? std::string("Session failed")
				   : fmt::format("Session failed: {}", reason(error));
	case ConnectionKind::AcceptFailed:
		return fmt::format("Accept failed: {}", reason(error));
	case ConnectionKind::AcceptLoopFailed:
		return fmt::format("Accept loop failed: {}", reason(error));
	case ConnectionKind::Protocol:
		return fmt::format("Protocol error: {}",
						   static_cast<resp::Parser::RespError>(error.code));
	}
	return "connection error";
}

ConnectionError connection_error(ConnectionKind kind,
								 const std::exception_ptr &error) {
	try {
		std::rethrow_exception(error);
	} catch (const boost::system::system_error &e) {
		return {.kind = kind, .cause = e.code()};
	} catch (const std::system_error &e) {
		return {.kind = kind, .cause = e.code()};
	} catch (const std::exception &e) {
		return {.kind = kind, .quoted = e.what()};
	} catch (...) { return {.kind = kind}; }
}

void log_error(const ConnectionError &error) {
	auto &logger = logging::logger_for(logging::channel::net);
	if (error.kind == ConnectionKind::AcceptLoopFailed)
		logger.critical("{}", error);
	else logger.error("{}", error);
}

} // namespace redis
