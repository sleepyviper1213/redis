#pragma once
// Makes co_await on Asio operations return (error_code, result) tuples.
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep
#include "server/commands/command.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstddef>

namespace redis {
class CommandHandler;
class Executor;

namespace net = boost::asio;

inline namespace v1 {

/**
 * \brief One client connection.
 *
 * Reads bytes, cuts them into requests (RESP arrays or inline commands),
 * executes every complete request and writes all replies back in one write,
 * so pipelined clients get one round trip per batch.
 *
 * The hot path is split in stages. Reading, parsing and writing touch only
 * this client's data and run on whichever I/O thread has the socket; only
 * execution touches the keyspace, so it goes through the Executor. Without a
 * dedicated executor thread the stages simply run back to back.
 *
 * A session lives in the frame of the serve() coroutine that runs it, so it
 * is destroyed when that coroutine finishes (or is destroyed): nothing else
 * refers to it and there is no shared ownership to manage.
 */
class Session {
public:
	/**
	 * \brief Serve the client on \p socket until it disconnects, a protocol
	 * error closes it, or the I/O context is destroyed.
	 *
	 * Spawn it on the socket's executor. \p executor, \p handler and \p ctx
	 * must outlive the coroutine.
	 */
	static net::awaitable<void> serve(net::ip::tcp::socket socket,
									  Executor &executor,
									  const CommandHandler &handler,
									  CommandContext &ctx);

	Session(const Session &)            = delete;
	Session &operator=(const Session &) = delete;

private:
	Session(net::ip::tcp::socket socket, Executor &executor,
			const CommandHandler &handler, CommandContext &ctx);

	net::awaitable<void> loop();

	static constexpr size_t READ_CHUNK_SIZE = 16uz * 1024;

	/**
	 * \brief Upper bound on buffered, not-yet-parsed bytes per client (same
	 * default as Redis' client-query-buffer-limit), so a slow or malicious
	 * client cannot grow server memory without bound.
	 */
	static constexpr size_t MAX_QUERY_BUFFER = 1024ULL * 1024 * 1024;

	net::ip::tcp::socket socket_;
	Executor &executor_;
	const CommandHandler &handler_;
	CommandContext &ctx_;
};
} // namespace v1
} // namespace redis
