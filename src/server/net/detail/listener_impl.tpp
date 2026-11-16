#pragma once
/**
 * \file
 * BasicListener's member functions. Included by listener.cpp, which
 * instantiates Listener, and by tests that instantiate a BasicListener on an
 * acceptor of their own.
 */

#include "accept_loop.hpp"
#include "server/log_channels.hpp"
#include "server/net/formatter.hpp" // IWYU pragma: keep (endpoint formatter)
#include "server/net/listener.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <exec/asio/use_sender.hpp>
#include <spdlog/logger.h>

#include <utility>


namespace redis {

template <Acceptor A>
BasicListener<A>::BasicListener(A acceptor, boost::asio::any_io_executor ex,
								std::shared_ptr<Services> services,
								bool concurrent)
	: ex_(std::move(ex)),
	  acceptor_(std::move(acceptor)),
	  services_(std::move(services)),
	  concurrent_(concurrent),
	  // On the acceptor's strand: the loop resumes where the wait completes,
	  // and must stay serialized with stop()'s close.
	  retry_timer_(acceptor_.get_executor()),
	  sessions_(ex_) {}

template <Acceptor A>
std::expected<void, ListenError>
BasicListener<A>::listen(std::string_view host,
						 boost::asio::ip::port_type port) {
	return listen_on(acceptor_, host, port);
}

// The loop itself is detail::accept_loop, built once over erased senders; what
// depends on the acceptor is only how to accept, and that is built here.
template <Acceptor A>
stdexec_ext::InlineTask<void> BasicListener<A>::run() {
	using boost::asio::ip::tcp;
	using stdexec::just;
	using stdexec::let_value;
	using stdexec::then;

	return detail::accept_loop(detail::AcceptHooks{
		.announce =
			[this] {
				boost::system::error_code bound;
				logging::logger_for(logging::channel::net)
					.info("Listening at {}", acceptor_.local_endpoint(bound));
			},
		.is_open = [this] { return acceptor_.is_open(); },
		// One accept. With several I/O threads each client gets a strand of its
		// own, so its handlers never overlap; a single thread needs no such
		// guarantee, and a strand would cost a lock and a queue hop on every
		// completion. Building the operation anew on every attempt is what
		// gives each client its own.
		.attempt = [this]() -> detail::AcceptAttempt {
			return just() | let_value([this] {
					   return acceptor_.async_accept(
						   concurrent_ ? boost::asio::any_io_executor(
											 boost::asio::make_strand(ex_))
									   : ex_,
						   exec::asio::use_sender);
				   }) |
				   then([this](tcp::socket client) {
					   sessions_.serve(std::move(client), services_);
				   });
		},
		// The error usually persists: wait a little rather than spin on it.
		.pause = [this]() -> detail::AcceptPause {
			retry_timer_.expires_after(detail::ACCEPT_RETRY_DELAY);
			return retry_timer_.async_wait(exec::asio::use_sender) |
				   then([this] { return acceptor_.is_open(); });
		}});
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
	sessions_.stop();
}

} // namespace redis
