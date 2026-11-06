#include "guard.hpp"

#include "core/logging.hpp"

#include <spdlog/logger.h>

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }

size_t limit_of(const NetLimits &limits, Violation violation) noexcept {
	switch (violation) {
	case Violation::TooManyClients: return limits.max_clients;
	case Violation::QueryBufferFull: return limits.max_query_buffer;
	case Violation::OutputBufferFull: return limits.max_output_buffer;
	case Violation::IdleTimeout:
		return static_cast<size_t>(limits.idle_timeout.count());
	case Violation::Count_: break;
	}
	return 0;
}
} // namespace

std::string_view to_string(Violation violation) noexcept {
	switch (violation) {
	case Violation::TooManyClients: return "too many clients";
	case Violation::QueryBufferFull: return "query buffer limit exceeded";
	case Violation::OutputBufferFull: return "output buffer limit exceeded";
	case Violation::IdleTimeout: return "idle timeout";
	case Violation::Count_: break;
	}
	return "unknown";
}

std::expected<NetGuard::Ticket, Violation> NetGuard::admit() noexcept {
	// Count first, undo on refusal: two threads racing for the last slot cannot
	// both get it.
	if (clients_.fetch_add(1, std::memory_order_relaxed) >=
		limits_.max_clients) {
		clients_.fetch_sub(1, std::memory_order_relaxed);
		return std::unexpected(Violation::TooManyClients);
	}
	return Ticket(this);
}

void NetGuard::record(Violation violation, std::string_view peer,
					  size_t observed) noexcept {
	counts_[static_cast<size_t>(violation)].fetch_add(
		1,
		std::memory_order_relaxed);
	log().warn("Closing {}: {} ({} against a limit of {})",
			   peer,
			   to_string(violation),
			   observed,
			   limit_of(limits_, violation));
}

} // namespace redis
