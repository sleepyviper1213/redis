#pragma once
// Makes co_await on Asio operations return (error_code, result) tuples.
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep
#include "server/commands/command.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstddef>
#include <memory>

namespace redis {
class CommandHandler;

namespace net = boost::asio;

/**
 * \brief One client connection.
 *
 * Reads bytes, cuts them into requests (RESP arrays or inline commands),
 * executes every complete request and writes all replies back in one write,
 * so pipelined clients get one round trip per batch.
 *
 * Instances are owned by std::shared_ptr and keep themselves alive while
 * their coroutine runs.
 */
class Session : public std::enable_shared_from_this<Session> {
public:
	/**
	 * \param socket A connected socket, moved into the session.
	 * \param handler, ctx Must outlive the session.
	 */
	Session(net::ip::tcp::socket socket, const CommandHandler &handler,
			CommandContext &ctx);

	Session(const Session &)            = delete;
	Session &operator=(const Session &) = delete;

	/// Start serving the client on the socket's executor.
	void run();

private:
	net::awaitable<void> loop();

	static constexpr size_t READ_CHUNK_SIZE = size_t{16} * 1024;

	/**
	 * \brief Upper bound on buffered, not-yet-parsed bytes per client (same
	 * default as Redis' client-query-buffer-limit), so a slow or malicious
	 * client cannot grow server memory without bound.
	 */
	static constexpr size_t MAX_QUERY_BUFFER = 1024ULL * 1024 * 1024;

	net::ip::tcp::socket socket_;
	const CommandHandler &handler_;
	CommandContext &ctx_;
};

} // namespace redis
