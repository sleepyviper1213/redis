#include "guard.hpp"

#include "server/log_channels.hpp"

#include <spdlog/logger.h>

#include <utility>

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

std::string_view format_as(Violation violation) noexcept {
	switch (violation) {
	case Violation::TooManyClients: return "too many clients";
	case Violation::QueryBufferFull: return "query buffer limit exceeded";
	case Violation::OutputBufferFull: return "output buffer limit exceeded";
	case Violation::IdleTimeout: return "idle timeout";
	case Violation::Count_: break;
	}
	return "unknown";
}

NetGuard::NetGuard(NetLimits limits) noexcept : limits_(limits) {}

std::expected<NetGuard::Ticket, Violation> NetGuard::admit() noexcept {
	// Count first, undo on refusal: two threads racing for the last slot cannot
	// both get it.
	if (clients_.fetch_add(1, std::memory_order_relaxed) >=
		limits_.max_clients) {
		clients_.fetch_sub(1, std::memory_order_relaxed);
		return std::unexpected(Violation::TooManyClients);
	}
	admitted_.fetch_add(1, std::memory_order_relaxed);
	return Ticket(this);
}

[[nodiscard]] std::expected<void, Violation>
NetGuard::check_input(size_t pending) const noexcept {
	if (pending > limits_.max_query_buffer)
		return std::unexpected(Violation::QueryBufferFull);
	return {};
}

[[nodiscard]] std::expected<void, Violation>
NetGuard::check_output(size_t replies) const noexcept {
	if (replies > limits_.max_output_buffer)
		return std::unexpected(Violation::OutputBufferFull);
	return {};
}

[[nodiscard]] const NetLimits &NetGuard::limits() const noexcept {
	return limits_;
}

void NetGuard::record(Violation violation, std::string_view peer,
					  size_t observed) noexcept {
	counts_[static_cast<size_t>(violation)].fetch_add(
		1,
		std::memory_order_relaxed);
	log().warn("Closing {}: {} ({} against a limit of {})",
			   peer,
			   violation,
			   observed,
			   limit_of(limits_, violation));
}

[[nodiscard]] uint64_t NetGuard::count(Violation violation) const noexcept {
	return counts_[static_cast<size_t>(violation)].load(
		std::memory_order_relaxed);
}

[[nodiscard]] uint64_t NetGuard::admitted() const noexcept {
	return admitted_.load(std::memory_order_relaxed);
}

[[nodiscard]] size_t NetGuard::clients() const noexcept {
	return clients_.load(std::memory_order_relaxed);
}

NetGuard::Ticket::Ticket(Ticket &&other) noexcept
	: owner_(std::exchange(other.owner_, nullptr)) {}

NetGuard::Ticket &NetGuard::Ticket::operator=(Ticket &&other) noexcept {
	if (this != &other) {
		release();
		owner_ = std::exchange(other.owner_, nullptr);
	}
	return *this;
}

NetGuard::Ticket::~Ticket() { release(); }

NetGuard::Ticket::Ticket(NetGuard *owner) noexcept : owner_(owner) {}

void NetGuard::Ticket::release() noexcept {
	if (owner_ != nullptr)
		owner_->clients_.fetch_sub(1, std::memory_order_relaxed);
	owner_ = nullptr;
}


} // namespace redis
