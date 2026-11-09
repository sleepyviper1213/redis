#pragma once
#include <boost/asio/ip/tcp.hpp>

#include <string>

namespace redis {
class NetGuard;

namespace detail {

/// "address:port" of the client on \p socket, or "unknown" if it has gone.
[[nodiscard]] std::string peer_name(const boost::asio::ip::tcp::socket &socket);

/**
 * \brief Turn away a client the server has no room for: record the violation
 * and tell the client why, best effort (as Redis does), without ever blocking.
 * The caller then drops the socket.
 */
void reject_over_capacity(boost::asio::ip::tcp::socket &socket,
						  NetGuard &guard);

} // namespace detail
} // namespace redis
