#pragma once
#include "net_error.hpp"
#include "nothrow_awaitable_t.hpp" // IWYU pragma: keep

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <expected>
#include <memory>
#include <string>

namespace redis {
struct Services;

namespace net = boost::asio;

/**
 * \brief Open \p acceptor on \p host:\p port (IPv4) and start listening.
 *
 * What the acceptor's own constructor does, but a port in use or a host that
 * is not an address comes back as a ListenError instead of an exception.
 * Shared by both Listeners.
 */
[[nodiscard]] std::expected<void, ListenError>
listen_on(net::ip::tcp::acceptor &acceptor, std::string_view host,
		  net::ip::port_type port);

inline namespace v1 {
/**
 * \brief Accepts TCP connections and starts a Session for each.
 *
 * Safe to run on several I/O threads: the acceptor lives on its own strand and
 * every session gets one of its own.
 */
class [[nodiscard]] Listener {
public:
	/// \p services is shared with every session. With \p concurrent false only
	/// one thread runs \p ex, so the strands that serialize handlers are left
	/// out. Call listen() before start().
	Listener(const net::any_io_executor &ex, std::shared_ptr<Services> services,
			 bool concurrent = true);

	/// Bind and listen on \p host:\p port (IPv4). Fails when the port is in use
	/// or \p host is not an address.
	[[nodiscard]] std::expected<void, ListenError>
	listen(std::string_view host, net::ip::port_type port);

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