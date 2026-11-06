#pragma once

#include <array>
#include <atomic>
#include <chrono>
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
	Count_, ///< Number of kinds; not a violation.
};

[[nodiscard]] std::string_view to_string(Violation violation) noexcept;

/// Per-server resource limits. The defaults are Redis's (maxclients,
/// client-query-buffer-limit, timeout), except for an output-buffer limit,
/// which Redis leaves unlimited for ordinary clients.
struct NetLimits {
	size_t max_clients       = 10'000;
	size_t max_query_buffer  = size_t{1} << 30;  // 1 GiB, as Redis
	size_t max_output_buffer = size_t{256} << 20; // 256 MiB
	/// Close a client that sends nothing for this long; zero never does.
	std::chrono::seconds idle_timeout{0};
};

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
		Ticket(Ticket &&other) noexcept
			: owner_(std::exchange(other.owner_, nullptr)) {}
		Ticket &operator=(Ticket &&other) noexcept {
			if (this != &other) {
				release();
				owner_ = std::exchange(other.owner_, nullptr);
			}
			return *this;
		}
		~Ticket() { release(); }

	private:
		friend class NetGuard;
		explicit Ticket(NetGuard *owner) noexcept : owner_(owner) {}
		void release() noexcept {
			if (owner_ != nullptr)
				owner_->clients_.fetch_sub(1, std::memory_order_relaxed);
			owner_ = nullptr;
		}
		NetGuard *owner_;
	};

	explicit NetGuard(NetLimits limits = {}) noexcept : limits_(limits) {}

	NetGuard(const NetGuard &)            = delete;
	NetGuard &operator=(const NetGuard &) = delete;

	/// A slot for one more client, or TooManyClients.
	[[nodiscard]] std::expected<Ticket, Violation> admit() noexcept;

	/// Whether \p pending buffered, unparsed bytes are within the limit.
	[[nodiscard]] std::expected<void, Violation>
	check_input(size_t pending) const noexcept {
		if (pending > limits_.max_query_buffer)
			return std::unexpected(Violation::QueryBufferFull);
		return {};
	}

	/// Whether \p replies bytes of unsent replies are within the limit.
	[[nodiscard]] std::expected<void, Violation>
	check_output(size_t replies) const noexcept {
		if (replies > limits_.max_output_buffer)
			return std::unexpected(Violation::OutputBufferFull);
		return {};
	}

	[[nodiscard]] const NetLimits &limits() const noexcept { return limits_; }

	/// Log and count a violation by the client \p peer. \p observed is the
	/// measured value that exceeded the limit (0 when there is none).
	void record(Violation violation, std::string_view peer,
				size_t observed) noexcept;

	/// How many times \p violation has been recorded.
	[[nodiscard]] uint64_t count(Violation violation) const noexcept {
		return counts_[static_cast<size_t>(violation)].load(
			std::memory_order_relaxed);
	}

	/// Clients currently admitted.
	[[nodiscard]] size_t clients() const noexcept {
		return clients_.load(std::memory_order_relaxed);
	}

private:
	NetLimits limits_;
	std::atomic<size_t> clients_{0};
	std::array<std::atomic<uint64_t>, static_cast<size_t>(Violation::Count_)>
		counts_{};
};

} // namespace redis
