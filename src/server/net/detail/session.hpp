#pragma once
// Makes co_await on Asio operations return (error_code, result) tuples.
#include "server/net/nothrow_awaitable_t.hpp" // IWYU pragma: keep

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "server/net/guard.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace redis {
struct Services;

namespace net = boost::asio;

namespace detail {
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
 * refers to it. Only the Services it works with are shared, one set for all
 * sessions, and the session keeps them alive while it runs.
 */
class Session {
public:
	/**
	 * \brief Serve the client on \p socket until it disconnects, a protocol
	 * error closes it, or the I/O context is destroyed.
	 *
	 * Spawn it on the socket's executor.
	 */
	static net::awaitable<void> serve(net::ip::tcp::socket socket,
									  std::shared_ptr<Services> services);

	Session(const Session &)            = delete;
	Session &operator=(const Session &) = delete;

private:
	Session(net::ip::tcp::socket socket, std::shared_ptr<Services> services,
			NetGuard::Ticket ticket);

	net::awaitable<void> loop();

	static constexpr size_t READ_CHUNK_SIZE = 16uz * 1024;

	net::ip::tcp::socket socket_;
	std::shared_ptr<Services> services_;
	/// Holds this client's slot under Services::guard until the session ends.
	NetGuard::Ticket ticket_;
	/// "address:port", for the log lines about this client.
	std::string peer_;
};
} // namespace v1
} // namespace detail
} // namespace redis
