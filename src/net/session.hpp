#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "core/database.hpp"
#include "net/tcp_server.hpp"
#include "nothrow_awaitable_t.hpp"
#include "utils/get_class_logger.hpp"
#include "utils/time.hpp"

#include <chrono>

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
	Session(net::ip::tcp::socket socket, Database &store,
			const CommandHandler &handler);

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
	net::awaitable<void> loop();

	void run();
	void close();

private:
	net::ip::tcp::socket socket_;

	timestamp_t connect_time;
	timestamp_t last_active;

	timespan_t idle_timeout = std::chrono::seconds(10);
	Database &database_;
	const CommandHandler &handler_;

	CLASS_LOGGER(Session);
};

std::string_view buffers_to_string(net::const_buffer buf);
} // namespace redis
