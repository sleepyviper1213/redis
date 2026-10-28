#include "tcp_server.hpp"

#include "core/logging.hpp"
#include "server/net/formatter.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/repeat_until.hpp>
#include <spdlog/logger.h>

#include <exception>
#include <utility>

namespace redis::v2 {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::net);
}
} // namespace

TcpServer::TcpServer(boost::asio::io_context &ioc, const std::string &host,
					 boost::asio::ip::port_type port, Executor &executor,
					 const CommandHandler &handler, CommandContext &ctx)
	: ioc_(ioc),
	  acceptor_(boost::asio::make_strand(ioc),
				boost::asio::ip::tcp::endpoint(
					boost::asio::ip::make_address_v4(host), port)),
	  executor_(executor),
	  handler_(handler),
	  ctx_(ctx),
	  drain_context_(ioc.get_executor()) {}

void TcpServer::start() {
	log().info("Listening at {}", acceptor_.local_endpoint());

	using boost::asio::ip::tcp;
	using stdexec::just;
	using stdexec::let_value;
	using stdexec::then;

	// A new strand per client, so its handlers never overlap even when several
	// I/O threads run the context. let_value builds the accept operation anew
	// on every iteration, which is what gives each client its own.
	stdexec::spawn(
		exec::repeat_until(
			just()
			| let_value([this] {
				  return acceptor_.async_accept(
					  boost::asio::any_io_executor(
						  boost::asio::make_strand(ioc_)),
					  exec::asio::use_sender);
			  })
			| then([this](tcp::socket client) {
				  stdexec::spawn(Session::serve(std::move(client), executor_,
												handler_, ctx_),
								 scope_.get_token());
				  return !acceptor_.is_open();
			  })
			// Transient failures (e.g. EMFILE when out of descriptors) must
			// not kill the accept loop, and the failed socket is unusable.
			// Cancellation, from stop(), is not an error and ends the loop.
			| stdexec::let_error([this](std::exception_ptr e) {
				  try {
					  std::rethrow_exception(e);
				  } catch (const std::exception &ex) {
					  log().error("Accept failed: {}", ex.what());
				  }
				  return just(!acceptor_.is_open());
			  }))
			// spawn() takes only senders that cannot fail. Nothing above
			// should, since errors are handled, but a callback may still throw.
			| stdexec::upon_error([](std::exception_ptr e) noexcept {
				  try {
					  std::rethrow_exception(e);
				  } catch (const std::exception &ex) {
					  log().critical("Accept loop failed: {}", ex.what());
				  }
			  }),
		scope_.get_token());
}

void TcpServer::stop() noexcept {
	// The acceptor is not thread-safe: close it on its own strand, where the
	// accept loop also runs.
	boost::asio::post(acceptor_.get_executor(), [this] {
		boost::system::error_code ignored;
		acceptor_.close(ignored);
	});
	scope_.request_stop();
}

} // namespace redis::v2
