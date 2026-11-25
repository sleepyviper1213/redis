#pragma once
/**
 * \file
 * What a session decides, apart from how it moves bytes (ADR-0012).
 *
 * No socket, scheduler, timer or sender in here, and no Asio or stdexec
 * header: a Conversation is fed bytes and says what to do next, so the
 * decisions can be tested from strings, in both modes, without a clock.
 */
#include "pipeline.hpp"
#include "server/net/guard.hpp"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace redis {
class CommandHandler;
struct CommandContext;

namespace detail {

/**
 * \brief One client's side of the protocol: the bytes it sent, the requests
 * cut from them, the replies owed, and whether the connection is over.
 *
 * The session reads into read_buffer(), calls ingest_read_bytes(), runs
 * execute_ingested_requests() (on the keyspace thread when the server has
 * one), sends unsent_replies(), and ends when is_over(). Everything it decides
 * is here; the limits are counted on the NetGuard, and what is logged about
 * the I/O stays with the session.
 *
 * Not thread-safe by itself. One session touches it at a time: its strand,
 * and the keyspace thread during execute_ingested_requests(), which the
 * executor's hand-off orders against the strand.
 */
class Conversation {
public:
	/// What a conversation decides with. Every reference outlives it: the
	/// session holds the Services that own them.
	struct Parts {
		// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
		NetGuard &guard;
		const CommandHandler &handler;
		CommandContext &ctx;
		/// As Executor::is_threaded(): parse here, execute on its thread.
		bool is_threaded;
		// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
	};

	/// \param peer "address:port", for the violations recorded about it.
	Conversation(Parts parts, std::string peer);

	/// Not copyable: the batch's requests view this conversation's own buffer.
	Conversation(const Conversation &)            = delete;
	Conversation &operator=(const Conversation &) = delete;
	Conversation(Conversation &&)                 = default;
	Conversation &operator=(Conversation &&)      = delete;
	~Conversation()                               = default;

	[[nodiscard]] std::span<char> read_buffer() noexcept;

	/**
	 * \brief Take in the first \p bytes of read_buffer().
	 * \return Whether there is something to run. False for nothing read, for
	 * (threaded) no complete request, and when the client is over its query
	 * buffer limit, which ends the conversation and is counted.
	 */
	[[nodiscard]] bool ingest_read_bytes(size_t bytes);

	/**
	 * \brief Execute what ingest_read_bytes() took in.
	 *
	 * Ends the conversation when the batch ended in a protocol error, or
	 * when the replies passed the output limit (they are dropped and
	 * counted). On the keyspace thread when the server has one.
	 */
	void execute_ingested_requests();

	[[nodiscard]] std::string_view unsent_replies() const noexcept;

	void discard_sent_replies() noexcept { replies_.clear(); }

	/// \param limit The idle limit that ran out, as counted.
	void count_idle_timeout_and_end(std::chrono::seconds limit);

	/// The request was not answered within \p limit: count it. Does not end
	/// the conversation or touch the replies, as work on them may still be
	/// unwinding; drop_replies_and_end() does that.
	void count_request_timeout(std::chrono::seconds limit);

	/// For replies that are incomplete or that nobody will read. Counts
	/// nothing.
	void drop_replies_and_end() noexcept;

	/// End, keeping the replies: an I/O failure the session has logged.
	void end_keeping_replies() noexcept { is_done_ = true; }

	[[nodiscard]] bool is_over() const noexcept;

	[[nodiscard]] std::string_view peer() const noexcept;

	static constexpr size_t READ_CHUNK_SIZE = 16uz * 1024;
	static_assert(READ_CHUNK_SIZE > 0, "a read needs room");

private:
	Parts parts_;
	/// "address:port", for the violations recorded about this client.
	std::string peer_;
	std::string chunk_;
	/// Bytes received and not yet consumed; the batch's requests view them.
	std::string pending_;
	std::string replies_;
	ParsedBatch batch_;
	AfterReplies after_ = AfterReplies::KeepOpen;
	bool is_done_          = false;
};

// A Session, and so its Conversation, is moved into its sender before it
// starts; nothing is viewed yet then (the batch is empty).
static_assert(std::is_move_constructible_v<Conversation>,
			  "a Session moves its Conversation into its sender");
static_assert(!std::is_copy_constructible_v<Conversation>,
			  "a copy would view the other's buffer");

} // namespace detail
} // namespace redis
