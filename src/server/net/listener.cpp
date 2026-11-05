#include "listener.hpp"

#include "core/logging.hpp"
#include "formatter.hpp" // IWYU pragma: keep (fmt formatters for endpoints and errors)
#include "services.hpp"
#include "session.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/error_code.hpp>
#include <fmt/format.h>
#include <spdlog/logger.h>

#include <chrono>
#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }

/// A session ends by exception only when something unexpected escaped it.
/// net::detached would drop that without a trace.
void report_session_failure(const std::exception_ptr &error) {
	if (!error) return;
	try {
		std::rethrow_exception(error);
	} catch (const std::exception &e) {
		log().error("Session failed: {}", e.what());
	} catch (...) {
		log().error("Session failed");
	}
}

/// How long to wait after a failed accept. The failure usually persists (out of
/// descriptors lasts until one is freed), and retrying at once would spin.
constexpr auto ACCEPT_RETRY_DELAY = std::chrono::milliseconds(100);
} // namespace

std::expected<void, std::string> listen_on(net::ip::tcp::acceptor &acceptor,
										   const std::string &host,
										   net::ip::port_type port) {
	boost::system::error_code error;
	const auto address = net::ip::make_address_v4(host, error);
	if (error)
		return std::unexpected(
			fmt::format("invalid host '{}': {}", host, error.message()));

	const net::ip::tcp::endpoint endpoint(address, port);
	acceptor.open(endpoint.protocol(), error);
	if (!error)
		acceptor.set_option(net::socket_base::reuse_address(true), error);
	if (!error) acceptor.bind(endpoint, error);
	if (!error)
		acceptor.listen(net::socket_base::max_listen_connections, error);
	if (error) {
		boost::system::error_code ignored;
		acceptor.close(ignored);
		return std::unexpected(fmt::format("cannot listen on {}:{}: {}",
										   host,
										   port,
										   error.message()));
	}
	return {};
}

inline namespace v1 {

Listener::Listener(const net::any_io_executor &ex,
					 std::shared_ptr<Services> services, bool concurrent)
	: acceptor_(net::make_strand(ex)),
	  services_(std::move(services)),
	  concurrent_(concurrent) {}

std::expected<void, std::string>
Listener::listen(const std::string &host, net::ip::port_type port) {
	return listen_on(acceptor_, host, port);
}

net::awaitable<void> Listener::start() {
	boost::system::error_code bound;
	log().info("Listening at {}", acceptor_.local_endpoint(bound));

	// The I/O executor this loop was spawned on.
	const net::any_io_executor io = co_await net::this_coro::executor;
	net::steady_timer retry_timer(io);
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
			retry_timer.expires_after(ACCEPT_RETRY_DELAY);
			co_await retry_timer.async_wait();
			continue;
		}
		const auto ex = client.get_executor();
		net::co_spawn(ex, Session::serve(std::move(client), services_),
					  report_session_failure);
	}
}

void Listener::stop() noexcept {
	// The acceptor is not thread-safe: close it on its own strand, where
	// start() is also running. Posting can only fail by running out of memory,
	// which ends the process (this is noexcept).
	net::post(acceptor_.get_executor(), [this] {
		boost::system::error_code ignored;
		acceptor_.close(ignored);
	});
}

} // namespace v1
} // namespace redis