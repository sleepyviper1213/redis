#include "session.hpp"

#include "admission.hpp"
#include "core/stdexec_ext/supervise.hpp"
#include "core/stdexec_ext/watchdog/time_limited.hpp"
#include "server/log_channels.hpp"
#include "server/net/net_error.hpp"
#include "server/services.hpp"
#include "timer_scheduler.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/variant_sender.hpp>
#include <spdlog/logger.h>

#include <cassert>
#include <chrono>
#include <exception>
#include <optional>
#include <span>
#include <utility>

using boost::asio::ip::tcp;

namespace redis::detail {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }

/**
 * \p sender if \p condition holds, otherwise a sender that completes at once
 * with nothing. let_value needs one sender type from all its branches, so the
 * two are made one. The unused sender is only built, never started.
 *
 * \tparam Sender The type of the sender to run conditionally (a
 *         stdexec::sender). It sends nothing, so that both alternatives
 *         complete the same way.
 * \return A sender; its type is deduced, a variant_sender of \p Sender and
 *         just().
 */
template <stdexec::sender Sender>
stdexec::sender auto run_if(bool condition, Sender sender) {
	using Skip   = decltype(stdexec::just());
	using Result = exec::variant_sender<Sender, Skip>;
	if (condition) return Result(std::move(sender));
	return Result(stdexec::just());
}

/**
 * \p work within \p limit, timed on \p timer: stdexec_ext::time_limited(),
 * whose contract this is, or \p work itself when there is no limit (\p limit is
 * zero, which is how the guard says "off"). Both the idle limit (the read)
 * and the request limit (executing and writing) are this race.
 *
 * The aliases exist because the two cases are different sender types and
 * variant_sender needs both named.
 *
 * \tparam Work The type of the sender raced against the clock, completing
 *         with one value.
 * \tparam OnExpiry What runs when the time runs out, returning a value of the
 *         type \p Work sends; see time_limited() for when it runs.
 * \return A sender; its type is deduced, a variant_sender of \p Work and the
 *         time limit.
 */
template <stdexec::sender Work, std::invocable OnExpiry>
stdexec::sender auto with_time_limit(Work work, TimerScheduler timer,
									 std::chrono::seconds limit,
									 OnExpiry on_expiry) {
	using Limited = decltype(stdexec_ext::time_limited(std::move(work),
													   timer,
													   limit,
													   std::move(on_expiry)));
	using Result  = exec::variant_sender<Work, Limited>;
	if (limit == std::chrono::seconds::zero()) return Result(std::move(work));
	return Result(stdexec_ext::time_limited(std::move(work),
											timer,
											limit,
											std::move(on_expiry)));
}
} // namespace

Session::Session(tcp::socket socket, std::shared_ptr<Services> services,
				 NetGuard::Ticket ticket)
	: socket_(std::move(socket)),
	  strand_(std::make_unique<AsioContext>(socket_.get_executor())),
	  idle_timer_(socket_.get_executor()),
	  request_timer_(socket_.get_executor()),
	  services_(std::move(services)),
	  ticket_(std::move(ticket)),
	  conversation_(
		  Conversation::Parts{.guard       = services_->guard,
							  .handler     = services_->handler,
							  .ctx         = services_->ctx,
							  .is_threaded = services_->keyspace.is_threaded()},
		  peer_name(socket_)) {
	// Needed for the immediate read_some in read(); async operations are
	// unaffected.
	boost::system::error_code ignored;
	socket_.non_blocking(true, ignored);
	log().debug("Accepted from {}", conversation_.peer());
}

void Session::fail_io(ConnectionKind kind, const std::exception_ptr &error) {
	conversation_.end_keeping_replies();
	const ConnectionError failure = connection_error(kind, error);
	switch (report_for(failure, socket_.is_open())) {
	case IoFailureReport::ClientLeft:
		log().debug("Client disconnected normally");
		break;
	// The server closed the socket itself (the request limit ran out, or it
	// is shutting down), and what was pending on it failed with it: already
	// accounted for, not news.
	case IoFailureReport::AfterServerClose:
		log().debug("{} from {} after the server closed the socket",
					failure,
					conversation_.peer());
		break;
	case IoFailureReport::Failure: log_error(failure); break;
	}
}

boost::asio::const_buffer Session::replies_buffer() const noexcept {
	const std::string_view replies = conversation_.unsent_replies();
	return {replies.data(), replies.size()};
}

Session::Outcome Session::read_available() {
	boost::system::error_code ec;
	const std::span<char> room = conversation_.read_buffer();
	const size_t n = socket_.read_some(
		boost::asio::buffer(room.data(), room.size()),
		ec);
	if (ec == boost::asio::error::would_block ||
		ec == boost::asio::error::try_again)
		return Outcome{.bytes = 0, .idle = false};
	if (ec) throw boost::system::system_error(ec);
	return Outcome{.bytes = n, .idle = false};
}

void Session::give_up_waiting_for_response(std::chrono::seconds limit) {
	conversation_.count_request_timeout(limit);
	log().debug("Request from {} not answered within {}s: closing",
				conversation_.peer(),
				limit.count());
	boost::system::error_code ignored;
	socket_.close(ignored);
}

stdexec::sender auto Session::read() {
	// Wait for readability, then read in place. async_read_some would first
	// try a speculative recv that, on a request/response connection, almost
	// always finds nothing yet: a wasted syscall per request (v1 measured it at
	// about a quarter of the server's time at one client).
	stdexec::sender auto bytes =
		socket_.async_wait(boost::asio::socket_base::wait_read,
						   exec::asio::use_sender) |
		stdexec::then([this] { return read_available(); }) |
		stdexec::let_error([this](const std::exception_ptr &error) {
			fail_io(ConnectionKind::ReadFailed, error);
			return stdexec::just(Outcome{.bytes = std::nullopt, .idle = false});
		});

	const auto idle = services_->guard.limits().idle_timeout;
	// Acting on the result after the race, not inside its branches, keeps what
	// each branch does to just reporting.
	return with_time_limit(
			   std::move(bytes),
			   TimerScheduler(*strand_, idle_timer_),
			   idle,
			   [] { return Outcome{.bytes = std::nullopt, .idle = true}; }) |
		   stdexec::then([this, idle](Outcome result) {
			   if (result.idle) conversation_.count_idle_timeout_and_end(idle);
			   return result.bytes;
		   });
}

stdexec::sender auto Session::execute() {
	// On the executor's thread and back on this strand afterwards, or right
	// here if there is no such thread.
	return services_->keyspace.run_on(
			   [this] { conversation_.execute_ingested_requests(); },
			   strand_->get_scheduler()) |
		   stdexec::let_error([this](const std::exception_ptr &error) {
			   fail_io(ConnectionKind::ExecuteFailed, error);
			   // Whatever replies the batch got to are incomplete: drop them
			   // with the client rather than send half an answer.
			   conversation_.drop_replies_and_end();
			   return stdexec::just();
		   });
}

stdexec::sender auto Session::write() {
	// Send what the socket takes at once: most replies fit, and then there is
	// no asynchronous write to start and no completion to wait for. Only the
	// rest, if any, goes through async_write.
	return stdexec::just() | stdexec::then([this] {
			   boost::system::error_code ec;
			   sent_ = socket_.write_some(replies_buffer(), ec);
			   assert(sent_ <= conversation_.unsent_replies().size() &&
					  "the socket took more than it was given");
			   // The socket is full for the moment: send it all asynchronously.
			   if (ec == boost::asio::error::would_block ||
				   ec == boost::asio::error::try_again) {
				   sent_ = 0;
				   return;
			   }
			   if (ec) throw boost::system::system_error(ec);
		   }) |
		   stdexec::let_value([this] {
			   return run_if(sent_ < conversation_.unsent_replies().size(),
							 boost::asio::async_write(socket_,
													  replies_buffer() + sent_,
													  exec::asio::use_sender) |
								 stdexec::then([](size_t) {}));
		   }) |
		   stdexec::then([this] {
			   log().trace("Sent {} bytes to {}",
						   conversation_.unsent_replies().size(),
						   conversation_.peer());
			   conversation_.discard_sent_replies();
		   }) |
		   stdexec::let_error([this](const std::exception_ptr &error) {
			   fail_io(ConnectionKind::WriteFailed, error);
			   return stdexec::just();
		   });
}

stdexec::sender auto Session::respond() {
	stdexec::sender auto answering =
		execute() | stdexec::let_value([this] {
			// A client whose request is still incomplete gets no reply.
			return run_if(!conversation_.unsent_replies().empty(), write());
		}) |
		stdexec::then([] { return false; }); // the limit did not run out

	const auto limit = services_->guard.limits().request_timeout;
	const auto give_up = [this, limit] {
		give_up_waiting_for_response(limit);
		return true;
	};
	return with_time_limit(std::move(answering),
						   TimerScheduler(*strand_, request_timer_),
						   limit,
						   give_up) |
		   stdexec::then([this](bool timed_out) {
			   if (!timed_out) return;
			   conversation_.drop_replies_and_end();
		   });
}

stdexec::sender auto Session::round_chain() {
	using stdexec::just;

	return read() | stdexec::let_value([this](std::optional<size_t> bytes) {
			   return run_if(bytes && conversation_.ingest_read_bytes(*bytes),
							 respond());
		   }) |
		   stdexec::let_error([this](const std::exception_ptr &error) {
			   fail_io(ConnectionKind::ConnectionFailed, error);
			   return just();
		   }) |
		   stdexec::then([this] { return conversation_.is_over(); });
}

// NOLINTBEGIN(readability-static-accessed-through-instance): false positive;
// clang-tidy reads the coroutine's co_await machinery as static member access.
// A coroutine for the loop only, as accept_loop() is: its frame is allocated
// once per session, and each round's operation state lives in it, so a round
// costs no allocation. It also ends the deduced sender types here: what is
// built on run() sees one InlineTask, not the tree of senders inside a round.
stdexec_ext::InlineTask<void> Session::run() {
	const stdexec::inplace_stop_token stop =
		co_await stdexec::read_env(stdexec::get_stop_token);
	// Here, where the session no longer moves.
	close_on_stop_.arm(socket_, stop);
	while (!co_await round_chain()) {}
}
// NOLINTEND(readability-static-accessed-through-instance)

SessionSender Session::serve(tcp::socket socket,
							 std::shared_ptr<Services> services) {
	auto ticket = services->guard.admit();
	if (!ticket) {
		reject_over_capacity(socket, services->guard);
		return stdexec::just();
	}
	stdexec::sender auto serving =
		stdexec::just(Session(std::move(socket),
							  std::move(services),
							  std::move(*ticket))) |
		stdexec::let_value([](Session &session) { return session.run(); });
	// One client's failure is that client's alone. The chain's only failures
	// are the ones round() did not already turn into "the session is over":
	// log them and finish normally.
	return stdexec_ext::supervised<stdexec_ext::FailPolicy::Isolate>(
		std::move(serving),
		[](const std::exception_ptr &e) noexcept {
			log_error(connection_error(ConnectionKind::SessionFailed, e));
		});
}

} // namespace redis::detail
