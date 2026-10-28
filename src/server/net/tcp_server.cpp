#include "tcp_server.hpp"

#include "core/logging.hpp"
#include "executor.hpp"
#include "formatter.hpp"
#include "session.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>

#include <utility>

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }
} // namespace

inline namespace v1 {

TcpServer::TcpServer(net::io_context &ioc, const std::string &host,
					 net::ip::port_type port, Executor &executor,
					 const CommandHandler &handler, CommandContext &ctx)
	: ioc_(ioc),
	  acceptor_(net::make_strand(ioc),
				net::ip::tcp::endpoint(net::ip::make_address_v4(host), port)),
	  executor_(executor),
	  handler_(handler),
	  ctx_(ctx) {}

net::awaitable<void> TcpServer::start() {
	log().info("Listening at {}", acceptor_.local_endpoint());

	while (acceptor_.is_open()) {
		// Each client gets its own strand, so its handlers never overlap even
		// when several I/O threads run the context.
		auto [err, client] = co_await acceptor_.async_accept(
			net::any_io_executor(net::make_strand(ioc_)));
		if (err == net::error::operation_aborted) co_return; // stop()
		if (err) {
			// Transient failures (e.g. EMFILE when out of descriptors) must
			// not kill the accept loop, and the failed socket is unusable.
			log().error("Accept failed: {}", err);
			continue;
		}
		const auto ex = client.get_executor();
		net::co_spawn(
			ex,
			Session::serve(std::move(client), executor_, handler_, ctx_),
			net::detached);
	}
}

void TcpServer::stop() noexcept {
	// The acceptor is not thread-safe: close it on its own strand, where
	// start() is also running.
	net::post(acceptor_.get_executor(), [this] {
		boost::system::error_code ignored;
		acceptor_.close(ignored);
	});
}

} // namespace v1
} // namespace redis