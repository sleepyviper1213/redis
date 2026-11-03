#include "tcp_server.hpp"

#include "core/logging.hpp"
#include "formatter.hpp" // IWYU pragma: keep (fmt formatters for endpoints and errors)
#include "services.hpp"
#include "session.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <spdlog/logger.h>

#include <memory>
#include <string>
#include <utility>

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }
} // namespace

inline namespace v1 {

TcpServer::TcpServer(const net::any_io_executor &ex, const std::string &host,
					 net::ip::port_type port,
					 std::shared_ptr<Services> services, bool concurrent)
	: acceptor_(net::make_strand(ex),
				net::ip::tcp::endpoint(net::ip::make_address_v4(host), port)),
	  services_(std::move(services)),
	  concurrent_(concurrent) {}

net::awaitable<void> TcpServer::start() {
	log().info("Listening at {}", acceptor_.local_endpoint());

	// The I/O executor this loop was spawned on.
	const net::any_io_executor io = co_await net::this_coro::executor;
	while (acceptor_.is_open()) {
		// With several I/O threads each client gets its own strand, so its
		// handlers never overlap. A single thread needs no such guarantee.
		auto [err, client] = co_await acceptor_.async_accept(
			concurrent_ ? net::any_io_executor(net::make_strand(io)) : io);
		if (err == net::error::operation_aborted) co_return; // stop()
		if (err) {
			// Transient failures (e.g. EMFILE when out of descriptors) must
			// not kill the accept loop, and the failed socket is unusable.
			log().error("Accept failed: {}", err);
			continue;
		}
		const auto ex = client.get_executor();
		net::co_spawn(ex, Session::serve(std::move(client), services_),
					  net::detached);
	}
}

// Only the logging in the catch can throw; ending the process on that is fine.
void TcpServer::stop() noexcept { // NOLINT(bugprone-exception-escape)
	// The acceptor is not thread-safe: close it on its own strand, where
	// start() is also running.
	try {
		net::post(acceptor_.get_executor(), [this] {
			boost::system::error_code ignored;
			acceptor_.close(ignored);
		});
	} catch (const std::exception &e) {
		log().error("Could not stop accepting: {}", e.what());
	}
}

} // namespace v1
} // namespace redis