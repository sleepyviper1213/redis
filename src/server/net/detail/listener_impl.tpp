#pragma once
/**
 * \file
 * BasicListener's member functions. Included by listener.cpp, which
 * instantiates Listener, and by tests that instantiate a BasicListener on an
 * acceptor of their own.
 */

#include "accept_loop.hpp"
#include "core/error/failed.hpp"
#include "server/log_channels.hpp"
#include "server/net/formatter.hpp" // IWYU pragma: keep (endpoint formatter)
#include "server/net/listener.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <exec/asio/use_sender.hpp>
#include <spdlog/logger.h>

#include <chrono>
#include <expected>
#include <memory>
#include <utility>

namespace redis {

template <Acceptor A>
BasicListener<A>::BasicListener(Token /*only create()*/, A acceptor,
								boost::asio::any_io_executor ex,
								std::shared_ptr<Services> services,
								bool is_concurrent)
	: ex_(std::move(ex)),
	  acceptor_(std::move(acceptor)),
	  services_(std::move(services)),
	  is_concurrent_(is_concurrent),
	  // On the acceptor's strand: the loop resumes where the wait completes,
	  // and must stay serialized with stop()'s close.
	  retry_timer_(acceptor_.get_executor()),
	  sessions_(ex_) {}

template <Acceptor A>
std::expected<std::unique_ptr<BasicListener<A>>, ListenError>
BasicListener<A>::create(A acceptor, boost::asio::any_io_executor ex,
						 std::string_view host, boost::asio::ip::port_type port,
						 std::shared_ptr<Services> services,
						 bool is_concurrent) {
	auto listener = std::make_unique<BasicListener>(Token{},
													std::move(acceptor),
													std::move(ex),
													std::move(services),
													is_concurrent);
	if (auto bound = listen_on(listener->acceptor_, host, port); !bound)
		REDIS_FAILED(bound.error());
	return listener;
}

// The loop itself is detail::accept_loop, built once over erased senders; what
// depends on the acceptor is only how to accept, and that is built here.
template <Acceptor A>
stdexec_ext::InlineTask<void> BasicListener<A>::run() {
	using boost::asio::ip::tcp;
	using stdexec::just;
	using stdexec::let_value;
	using stdexec::then;

	// On the acceptor's strand, as the loop starts.
	boost::system::error_code bound;
	logging::logger_for(logging::channel::net)
		.info("Listening at {}", acceptor_.local_endpoint(bound));

	/// What the loop asks of this listener; it lives in the coroutine's frame, as
	/// long as the loop runs.
	class Source final : public detail::AcceptSource {
	public:
		explicit Source(BasicListener &listener) : listener_(listener) {}

		[[nodiscard]] bool is_open() const override {
			return listener_.acceptor_.is_open();
		}

		// One accept. With several I/O threads each client gets a strand of its
		// own, so its handlers never overlap; a single thread needs no such
		// guarantee, and a strand would cost a lock and a queue hop on every
		// completion. Building the operation anew on every attempt is what
		// gives each client its own.
		[[nodiscard]] detail::AcceptAttempt attempt() override {
			BasicListener &listener = listener_;
			return just() | let_value([&listener] {
					   return listener.acceptor_.async_accept(
						   listener.is_concurrent_
							   ? boost::asio::any_io_executor(
									 boost::asio::make_strand(listener.ex_))
							   : listener.ex_,
						   exec::asio::use_sender);
				   }) |
				   then([&listener](tcp::socket client) {
					   listener.sessions_.serve(std::move(client),
												listener.services_);
				   });
		}

		// The error usually persists: wait rather than spin on it, as long as
		// the loop says.
		[[nodiscard]] detail::AcceptPause
		pause(std::chrono::milliseconds wait) override {
			BasicListener &listener = listener_;
			listener.retry_timer_.expires_after(wait);
			return listener.retry_timer_.async_wait(exec::asio::use_sender) |
				   then([&listener] { return listener.acceptor_.is_open(); });
		}

	private:
		BasicListener &listener_;
	};

	Source source(*this);
	co_await detail::accept_loop(source);
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
