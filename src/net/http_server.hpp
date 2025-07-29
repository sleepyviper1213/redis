#pragma once
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace redis {

class HTTPQueryHandler;

namespace net = boost::asio;
using tcp     = net::ip::tcp;

/**
 * @brief Handles a single HTTP session on an accepted TCP socket.
 *
 * Reads an HTTP request, delegates it to the provided `HTTPQueryHandler`,
 * and sends the response back on the same stream.
 *
 * @param socket TCP socket connected to the client.
 * @param dqr The handler used to process incoming HTTP requests.
 * @return A coroutine that completes after handling the session.
 */
net::awaitable<void> handle_session(tcp::socket socket, HTTPQueryHandler dqr);

/**
 * @brief Starts a TCP listener coroutine that accepts and handles HTTP
 * sessions.
 *
 * Uses `co_spawn` to run `handle_session()` for each incoming connection.
 *
 * @param endpoint The local endpoint (IP:port) to bind the listener.
 * @return A coroutine that runs indefinitely, accepting connections.
 */
net::awaitable<void> listener(tcp::endpoint endpoint);
} // namespace redis
