#pragma once
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <memory>
#include <string>

namespace redis {
struct Services;

namespace net = boost::asio;

inline namespace v1 {
/**
 * \brief Accepts TCP connections and starts a Session for each.
 *
 * Safe to run on several I/O threads: the acceptor lives on its own strand and
 * every session gets one of its own.
 */
class [[nodiscard]] TcpServer {
public:
	/// Bind and listen on \p host:\p port (IPv4). Throws on failure, e.g.
	/// when the port is in use. \p services is shared with every session. With
	/// \p concurrent false only one thread runs \p ex, so the strands that
	/// serialize handlers are left out.
	TcpServer(const net::any_io_executor &ex, const std::string &host,
			  net::ip::port_type port, std::shared_ptr<Services> services,
			  bool concurrent = true);

	/// Accept connections until stop() is called.
	net::awaitable<void> start();

	/// Stop accepting. Established sessions keep running. May be called from
	/// any thread.
	void stop() noexcept;

private:
	net::ip::tcp::acceptor acceptor_;
	std::shared_ptr<Services> services_;
	bool concurrent_;
};
} // namespace v1

} // namespace redis