/**
 * \file
 * \brief Session interface handling a single client connection.
 */

#pragma once
#include "nothrow_awaitable_t.hpp"
#include "redis_store.hpp"
#include "utils/time.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <memory>

namespace net = boost::asio;

namespace redis {

/**
 * \brief Represents one TCP client session.
 *
 * Owns a socket for a single client connection and coordinates request
 * handling against a shared RedisStore instance. Instances are intended to be
 * managed via std::shared_ptr and can create shared references to themselves
 * using std::enable_shared_from_this.
 */
class Session : public std::enable_shared_from_this<Session> {
public:
	/**
	 * \brief Construct a Session for an accepted socket.
	 * \param socket A connected TCP socket transferred into the session.
	 * \param store Reference to the backing RedisStore used to serve requests.
	 * \note Ownership of \p socket is moved to the Session. The caller remains
	 * responsible for ensuring that \p store outlives this Session.
	 */
	Session(net::ip::tcp::socket socket, RedisStore &store);

	/**
	 * \name Non-copyable, movable
	 * @{
	 */
	Session(const Session &)            = delete;
	Session(Session &&)                 = default;
	Session &operator=(const Session &) = delete;
	Session &operator=(Session &&)      = delete;
	/** @} */

	/**
	 * \brief Run the session event loop.
	 * \details Asynchronously processes the connection until it is closed
	 * or an unrecoverable error occurs.
	 * \return An awaitable that completes when the session finishes.
	 */
	net::awaitable<void> run();
	net::awaitable<void> loop(timestamp_t &deadline);
	net::awaitable<void> watchdog(timestamp_t &deadline);

	void close();

private:
	net::ip::tcp::socket socket_;

	timestamp_t connect_time;
	timestamp_t last_active;

	timespan_t idle_timeout = std::chrono::seconds(10);
	RedisStore &store_;
};
} // namespace redis
