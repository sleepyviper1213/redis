#include "listener.hpp"

#include "core/stdexec_ext/retry.hpp"
#include "server/log_channels.hpp"
#include "server/net/formatter.hpp"
#include "server/net/listener.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/repeat_until.hpp>
#include <spdlog/logger.h>

#include <chrono>
#include <exception>
#include <utility>

namespace redis::v2 {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }

/// How long to wait after a failed accept. The failure usually persists (out of
/// descriptors lasts until one is freed), and retrying at once would spin.
constexpr auto ACCEPT_RETRY_DELAY = std::chrono::milliseconds(100);
} // namespace

Listener::Listener(boost::asio::any_io_executor ex,
				   std::shared_ptr<Services> services, bool concurrent)
	: ex_(std::move(ex)),
	  acceptor_(boost::asio::make_strand(ex_)),
	  services_(std::move(services)),
	  concurrent_(concurrent),
	  retry_timer_(ex_),
	  drain_context_(ex_) {}

std::expected<void, ListenError>
Listener::listen(std::string_view host, boost::asio::ip::port_type port) {
	return listen_on(acceptor_, host, port);
}

void Listener::start() {
	boost::system::error_code bound;
	log().info("Listening at {}", acceptor_.local_endpoint(bound));

	using boost::asio::ip::tcp;
	using stdexec::just;
	using stdexec::let_value;
	using stdexec::then;

	// One accept. With several I/O threads each client gets a strand of its own,
	// so its handlers never overlap; a single thread needs no such guarantee,
	// and a strand would cost a lock and a queue hop on every completion.
	// Building the operation anew on every attempt is what gives each client
	// its own.
	stdexec_ext::AttemptFactory auto accept_one = [this] {
		return just() | let_value([this] {
				   return acceptor_.async_accept(
					   concurrent_ ? boost::asio::any_io_executor(
										 boost::asio::make_strand(ex_))
								   : ex_,
					   exec::asio::use_sender);
			   }) |
			   then([this](tcp::socket client) {
				   stdexec::spawn(
					   detail::v2::Session::serve(std::move(client), services_),
					   scope_.get_token());
			   });
	};

	// Transient failures (e.g. EMFILE when out of descriptors) must not kill
	// the accept loop, and the failed socket is unusable: log, wait, try again.
	// Once the acceptor is closed there is nothing to retry. Cancellation, from
	// stop(), is not an error and ends the wait.
	stdexec_ext::RetryPolicy auto after_failure =
		[this](size_t, const std::exception_ptr &e) {
			try {
				std::rethrow_exception(e);
			} catch (const std::exception &ex) {
				log_error({.kind   = ConnectionKind::AcceptFailed,
						   .quoted = ex.what()});
			}
			// The error usually persists: wait a little rather than spin on it.
			retry_timer_.expires_after(ACCEPT_RETRY_DELAY);
			return retry_timer_.async_wait(exec::asio::use_sender) |
				   then([this] { return acceptor_.is_open(); });
		};

	stdexec::spawn(
		exec::repeat_until(
			stdexec_ext::retry(accept_one, after_failure) |
			then([this] { return !acceptor_.is_open(); })
			// retry only gives up once the acceptor is closed: the loop is
			// over.
			| stdexec::upon_error(
				  [](const std::exception_ptr &) noexcept { return true; }))
			// spawn() takes only senders that cannot fail. Nothing above
			// should, since errors are handled, but a callback may still throw.
			| stdexec::upon_error([](const std::exception_ptr &e) noexcept {
				  try {
					  std::rethrow_exception(e);
				  } catch (const std::exception &ex) {
					  log_error({.kind   = ConnectionKind::AcceptLoopFailed,
								 .quoted = ex.what()});
				  }
			  }),
		scope_.get_token());
}

void Listener::stop() noexcept {
	// The acceptor is not thread-safe: close it on its own strand, where the
	// accept loop also runs. Posting can only fail by running out of memory,
	// which ends the process (this is noexcept).
	boost::asio::post(acceptor_.get_executor(), [this] {
		boost::system::error_code ignored;
		acceptor_.close(ignored);
	});
	scope_.request_stop();
}

} // namespace redis::v2
