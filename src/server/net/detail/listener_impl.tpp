#pragma once
/**
 * \file
 * BasicListener's member functions. Included by listener.cpp, which
 * instantiates Listener, and by tests that instantiate a BasicListener on an
 * acceptor of their own.
 */

#include "server/net/listener.hpp"

#include "core/stdexec_ext/retry.hpp"
#include "server/log_channels.hpp"
#include "server/net/formatter.hpp" // IWYU pragma: keep (endpoint formatter)

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <exec/asio/use_sender.hpp>
#include <spdlog/logger.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <utility>

namespace redis {
namespace detail {

/// How long to wait after a failed accept. The failure usually persists (out of
/// descriptors lasts until one is freed), and retrying at once would spin.
inline constexpr auto ACCEPT_RETRY_DELAY = std::chrono::milliseconds(100);

/// Logs what a failed accept said. Errors travel as exception_ptrs, so the
/// only way to read one is to throw it again.
inline void log_accept_failure(const std::exception_ptr &error) {
	try {
		std::rethrow_exception(error);
	} catch (const std::exception &ex) {
		log_error({.kind = ConnectionKind::AcceptFailed, .quoted = ex.what()});
	}
}

} // namespace detail

template <Acceptor A>
BasicListener<A>::BasicListener(A acceptor, boost::asio::any_io_executor ex,
								std::shared_ptr<Services> services,
								bool concurrent)
	: ex_(std::move(ex)),
	  acceptor_(std::move(acceptor)),
	  services_(std::move(services)),
	  concurrent_(concurrent),
	  retry_timer_(ex_),
	  drain_context_(ex_) {}

template <Acceptor A>
std::expected<void, ListenError>
BasicListener<A>::listen(std::string_view host,
						 boost::asio::ip::port_type port) {
	return listen_on(acceptor_, host, port);
}

// A coroutine for the loop only: it runs once per accept or failure, not per
// request, so its frame is not on the request path. Sessions stay sender
// chains. Cancellation, from stop(), reaches the suspended co_await as
// set_stopped, which unwinds the whole task without an error.
template <Acceptor A>
stdexec_ext::InlineTask<void> BasicListener<A>::run() {
	using boost::asio::ip::tcp;
	using stdexec::just;
	using stdexec::let_value;
	using stdexec::then;

	// One accept. With several I/O threads each client gets a strand of its
	// own, so its handlers never overlap; a single thread needs no such
	// guarantee, and a strand would cost a lock and a queue hop on every
	// completion. Building the operation anew on every attempt is what gives
	// each client its own.
	stdexec_ext::AttemptFunc auto accept_one = [this] {
		return just() | let_value([this] {
				   return acceptor_.async_accept(
					   concurrent_ ? boost::asio::any_io_executor(
										 boost::asio::make_strand(ex_))
								   : ex_,
					   exec::asio::use_sender);
			   }) |
			   then([this](tcp::socket client) {
				   stdexec::spawn(
					   detail::Session::serve(std::move(client), services_),
					   scope_.get_token());
			   });
	};

	// Transient failures (e.g. EMFILE when out of descriptors) must not kill
	// the loop, and the failed socket is unusable: log, wait, try again. Once
	// the acceptor is closed there is nothing to retry, and that is how the
	// loop is told to end, not a failure to report.
	stdexec_ext::RetryFunc auto after_failure =
		[this](size_t, const std::exception_ptr &error) {
			if (acceptor_.is_open()) detail::log_accept_failure(error);
			// The error usually persists: wait a little rather than spin on it.
			retry_timer_.expires_after(detail::ACCEPT_RETRY_DELAY);
			return retry_timer_.async_wait(exec::asio::use_sender) |
				   then([this] { return acceptor_.is_open(); });
		};

	boost::system::error_code bound;
	logging::logger_for(logging::channel::net)
		.info("Listening at {}", acceptor_.local_endpoint(bound));

	while (acceptor_.is_open()) {
		try {
			co_await stdexec_ext::retry(accept_one, after_failure);
		} catch (const std::exception &) {
			co_return; // retry gives up only once the acceptor is closed
		}
	}
}

template <Acceptor A>
// NOLINTNEXTLINE(bugprone-exception-escape): out of memory only; see below.
void BasicListener<A>::stop() noexcept {
	// The acceptor is not thread-safe: close it on its own strand, where the
	// accept loop also runs. Posting can only fail by running out of memory,
	// which ends the process (this is noexcept).
	boost::asio::post(acceptor_.get_executor(), [this] {
		boost::system::error_code ignored;
		acceptor_.close(ignored);
	});
	scope_.request_stop();
}

} // namespace redis
