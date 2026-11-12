#pragma once

#include "net_limits.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

namespace redis {

/// Why a connection was refused or closed by the network guard.
enum class Violation : uint8_t {
	TooManyClients,
	QueryBufferFull,
	OutputBufferFull,
	IdleTimeout,
	RequestTimeout,
	Count_, ///< Number of kinds; not a violation.
};

[[nodiscard]] std::string_view format_as(Violation violation) noexcept;

/**
 * \brief The admission-control layer between the network and the session logic.
 *
 * Sessions ask it, and only it, whether a connection may exist and whether its
 * buffers are within bounds. Every refusal is a Violation, reported through
 * the one place that logs and counts it (record()). The checks are pure and
 * return before any command runs, so a rejected batch never leaves partial
 * effects behind.
 *
 * One per server, shared by all sessions and I/O threads.
 */
class NetGuard {
public:
	/// Proof of admission; releases the client slot when destroyed.
	class [[nodiscard]] Ticket {
	public:
		Ticket(const Ticket &)            = delete;
		Ticket &operator=(const Ticket &) = delete;

		Ticket(Ticket &&other) noexcept;

		Ticket &operator=(Ticket &&other) noexcept;

		~Ticket();

	private:
		friend class NetGuard;

		explicit Ticket(NetGuard *owner) noexcept;

		void release() noexcept;

		NetGuard *owner_;
	};

	explicit NetGuard(NetLimits limits = {}) noexcept;

	NetGuard(const NetGuard &)            = delete;
	NetGuard &operator=(const NetGuard &) = delete;

	/// A slot for one more client, or TooManyClients.
	[[nodiscard]] std::expected<Ticket, Violation> admit() noexcept;

	/// Whether \p pending buffered, unparsed bytes are within the limit.
	[[nodiscard]] std::expected<void, Violation>
	check_input(size_t pending) const noexcept;

	/// Whether \p replies bytes of unsent replies are within the limit.
	[[nodiscard]] std::expected<void, Violation>
	check_output(size_t replies) const noexcept;

	[[nodiscard]] const NetLimits &limits() const noexcept;

	/// Log and count a violation by the client \p peer. \p observed is the
	/// measured value that exceeded the limit (0 when there is none).
	void record(Violation violation, std::string_view peer,
				size_t observed) noexcept;

	/// How many times \p violation has been recorded.
	[[nodiscard]] uint64_t count(Violation violation) const noexcept;

	/// Connections admitted since the server started.
	[[nodiscard]] uint64_t admitted() const noexcept;

	/// Clients currently admitted.
	[[nodiscard]] size_t clients() const noexcept;

private:
	NetLimits limits_;
	std::atomic<size_t> clients_{0};
	std::atomic<uint64_t> admitted_{0};
	std::array<std::atomic<uint64_t>, static_cast<size_t>(Violation::Count_)>
		counts_{};
};

} // namespace redis
