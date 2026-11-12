#include "session.hpp"

#include "core/stdexec_ext/supervise.hpp"
#include "server/log_channels.hpp"
#include "server/net/detail/admission.hpp"
#include "server/net/detail/pipeline.hpp"
#include "server/net/formatter.hpp"
#include "server/net/net_error.hpp"
#include "server/services.hpp"

#include <boost/asio/write.hpp>
#include <exec/asio/use_sender.hpp>
#include <exec/repeat_until.hpp>
#include <exec/unless_stop_requested.hpp>
#include <exec/variant_sender.hpp>
#include <exec/when_any.hpp>
#include <spdlog/logger.h>

#include <chrono>
#include <exception>
#include <optional>
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

/// How the wait for a request ends. Plain data: it is built by the stages of
/// the race, which therefore do nothing but report.
struct Outcome {
	std::optional<size_t> bytes; ///< Nothing once the session is over.
	bool idle = false;           ///< The idle timer won.
};

/**
 * \p work, abandoned if \p limit passes first: whichever finishes first
 * cancels the other through the stop token, and the race completes once both
 * have unwound, with the winner's value. With no limit (\p limit is zero) it
 * is \p work itself. Both the idle limit (the read) and the request limit
 * (executing and writing) are this race.
 *
 * The timer is armed when the race starts, not when the sender is built:
 * repeat_until restarts one sender every round, so building it runs once.
 * The aliases exist because the two cases are different sender types and
 * variant_sender needs both named.
 *
 * The race ends only once \p work has unwound, so \p work must finish
 * promptly when stopped. When a stop request alone cannot promise that,
 * \p on_expiry is where to force it (see respond()).
 *
 * \tparam Work The type of the sender raced against the clock (a
 *         stdexec::sender), completing with one value.
 * \tparam OnExpiry The type of what runs when the time runs out: a callable
 *         taking nothing, returning a value of the type \p Work sends, so
 *         that both branches complete the same way.
 * \return A sender; its type is deduced, a variant_sender of \p Work and the
 *         race between it and the timer.
 */
template <stdexec::sender Work, std::invocable OnExpiry>
stdexec::sender auto
with_time_limit(Work work, boost::asio::steady_timer &timer,
				std::chrono::seconds limit, OnExpiry on_expiry) {
	// unless_stop_requested: when work completes at once, as it does with
	// nothing to answer yet, the stop request reaches the timer before its wait
	// starts. use_sender would lose it (its cancellation signal has no handler
	// yet) and the race would wait out the whole limit, every round.
	stdexec::sender auto expiry =
		stdexec::just() | stdexec::let_value([&timer, limit] {
			timer.expires_after(limit);
			return exec::unless_stop_requested(
				timer.async_wait(exec::asio::use_sender));
		}) |
		stdexec::then(std::move(on_expiry));
	using Raced  = decltype(exec::when_any(std::move(work), std::move(expiry)));
	using Result = exec::variant_sender<Work, Raced>;
	if (limit == std::chrono::seconds::zero()) return Result(std::move(work));
	return Result(exec::when_any(std::move(work), std::move(expiry)));
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
	  peer_(peer_name(socket_)),
	  chunk_(READ_CHUNK_SIZE, '\0') {
	// Needed for the immediate read_some in read(); async operations are
	// unaffected.
	boost::system::error_code ignored;
	socket_.non_blocking(true, ignored);
	log().debug("Accepted from {}", peer_);
}

bool Session::ingest(size_t bytes) {
	// The previous batch has run, so the bytes its requests viewed can go
	// (execute_pending has already dropped them when there is no executor
	// thread).
	drop_consumed(pending_, batch_);
	pending_.append(chunk_.data(), bytes);
	if (!services_->guard.check_input(pending_.size())) {
		services_->guard.record(Violation::QueryBufferFull,
								peer_,
								pending_.size());
		done_ = true;
		return false;
	}
	// Without an executor thread, execute_pending parses and executes in one.
	if (!services_->keyspace.is_threaded()) return true;

	// Parse here on the strand, so other sessions parse while this batch
	// executes.
	parse_pending(pending_, batch_);
	return batch_.size() != 0 || !batch_.protocol_error.empty();
}

void Session::run_batch() {
	Services &services      = *services_;
	const size_t max_output = services.guard.limits().max_output_buffer;
	after_ = services.keyspace.is_threaded() ? execute_batch(batch_,
															 replies_,
															 services.handler,
															 services.ctx,
															 max_output)
											 : execute_pending(pending_,
															   batch_,
															   replies_,
															   services.handler,
															   services.ctx,
															   max_output);
	if (!services.guard.check_output(replies_.size())) {
		// Nobody is going to read this much: drop it with the client.
		services.guard.record(Violation::OutputBufferFull,
							  peer_,
							  replies_.size());
		replies_.clear();
		done_ = true;
	}
}

void Session::fail(const std::exception_ptr &error) {
	done_ = true;
	try {
		std::rethrow_exception(error);
	} catch (const boost::system::system_error &e) {
		if (e.code() == boost::asio::error::eof)
			log().debug("Client disconnected normally");
		else
			log_error(
				{.kind = ConnectionKind::ConnectionFailed, .cause = e.code()});
	} catch (const std::exception &e) {
		log_error(
			{.kind = ConnectionKind::ConnectionFailed, .quoted = e.what()});
	}
}

void Session::fail_io(ConnectionKind kind, const std::exception_ptr &error) {
	done_ = true;
	try {
		std::rethrow_exception(error);
	} catch (const boost::system::system_error &e) {
		if (e.code() == boost::asio::error::eof)
			log().debug("Client disconnected normally");
		else log_error({.kind = kind, .cause = e.code()});
	} catch (const std::exception &e) {
		log_error({.kind = kind, .quoted = e.what()});
	}
}

stdexec::sender auto Session::read() {
	// Wait for readability, then read in place. async_read_some would first
	// try a speculative recv that, on a request/response connection, almost
	// always finds nothing yet: a wasted syscall per request (v1 measured it at
	// about a quarter of the server's time at one client).
	stdexec::sender auto bytes =
		socket_.async_wait(boost::asio::socket_base::wait_read,
						   exec::asio::use_sender) |
		stdexec::then([this] {
			boost::system::error_code ec;
			const size_t n = socket_.read_some(boost::asio::buffer(chunk_), ec);
			// Readiness can be spurious: count it as a round that read nothing,
			// which ingests nothing and goes back to waiting.
			if (ec == boost::asio::error::would_block ||
				ec == boost::asio::error::try_again)
				return Outcome{.bytes = 0, .idle = false};
			if (ec) throw boost::system::system_error(ec);
			return Outcome{.bytes = n, .idle = false};
		}) |
		stdexec::let_error([this](const std::exception_ptr &error) {
			fail_io(ConnectionKind::ReadFailed, error);
			return stdexec::just(Outcome{.bytes = std::nullopt, .idle = false});
		});

	const auto idle = services_->guard.limits().idle_timeout;
	// Acting on the result after the race, not inside its branches, keeps what
	// each branch does to just reporting.
	return with_time_limit(
			   std::move(bytes),
			   idle_timer_,
			   idle,
			   [] { return Outcome{.bytes = std::nullopt, .idle = true}; }) |
		   stdexec::then([this, idle](Outcome result) {
			   if (result.idle) {
				   services_->guard.record(Violation::IdleTimeout,
										   peer_,
										   static_cast<size_t>(idle.count()));
				   done_ = true;
			   }
			   return result.bytes;
		   });
}

stdexec::sender auto Session::execute() {
	// On the executor's thread and back on this strand afterwards, or right
	// here if there is no such thread.
	return services_->keyspace.run_on([this] { run_batch(); },
									  strand_->get_scheduler()) |
		   stdexec::let_error([this](const std::exception_ptr &error) {
			   fail_io(ConnectionKind::ExecuteFailed, error);
			   // Whatever replies the batch got to are incomplete: drop them
			   // with the client rather than send half an answer.
			   replies_.clear();
			   return stdexec::just();
		   });
}

stdexec::sender auto Session::write() {
	// Send what the socket takes at once: most replies fit, and then there is
	// no asynchronous write to start and no completion to wait for. Only the
	// rest, if any, goes through async_write.
	return stdexec::just() | stdexec::then([this] {
			   boost::system::error_code ec;
			   sent_ = socket_.write_some(boost::asio::buffer(replies_), ec);
			   // The socket is full for the moment: send it all asynchronously.
			   if (ec == boost::asio::error::would_block ||
				   ec == boost::asio::error::try_again) {
				   sent_ = 0;
				   return;
			   }
			   if (ec) throw boost::system::system_error(ec);
		   }) |
		   stdexec::let_value([this] {
			   return run_if(sent_ < replies_.size(),
							 boost::asio::async_write(
								 socket_,
								 boost::asio::buffer(replies_) + sent_,
								 exec::asio::use_sender) |
								 stdexec::then([](size_t) {}));
		   }) |
		   stdexec::then([this] {
			   log().trace("Sent {} bytes to {}", replies_.size(), peer_);
			   replies_.clear();
		   }) |
		   stdexec::let_error([this](const std::exception_ptr &error) {
			   fail_io(ConnectionKind::WriteFailed, error);
			   return stdexec::just();
		   });
}

stdexec::sender auto Session::respond() {
	stdexec::sender auto answering =
		execute() | stdexec::let_value([this] {
			if (after_ == AfterReplies::Close) done_ = true;
			// A client whose request is still incomplete gets no reply.
			return run_if(!replies_.empty(), write());
		}) |
		stdexec::then([] { return false; }); // the limit did not run out

	const auto limit = services_->guard.limits().request_timeout;
	// What a timeout cuts short depends on where it finds the batch, which is
	// the delivery guarantee: one still queued for the keyspace thread never
	// runs (the pool checks the stop request before running it), one already
	// running completes, and a write in progress is abandoned. Either way the
	// replies are dropped with the client.
	// Out of time, close the socket rather than only cancel the write: a stop
	// request asks Asio to cancel the send, and Windows does not complete a
	// send the peer is not reading, so the race would wait for it forever.
	// On a closed socket every pending operation completes. This runs on the
	// session's strand, so it cannot overlap the write's own handlers, and the
	// session is over anyway.
	// Counted here, when the limit runs out, not once the race is over: that
	// waits for the abandoned work to unwind, which for a batch queued behind a
	// long one is as late as that one finishes.
	const auto give_up = [this, limit] {
		services_->guard.record(Violation::RequestTimeout,
								peer_,
								static_cast<size_t>(limit.count()));
		log().debug("Request from {} not answered within {}s: closing",
					peer_,
					limit.count());
		boost::system::error_code ignored;
		socket_.close(ignored);
		return true;
	};
	return with_time_limit(std::move(answering),
						   request_timer_,
						   limit,
						   give_up) |
		   stdexec::then([this](bool timed_out) {
			   if (!timed_out) return;
			   replies_.clear();
			   done_ = true;
		   });
}

stdexec::sender auto Session::round() {
	using stdexec::just;

	return read() | stdexec::let_value([this](std::optional<size_t> bytes) {
			   return run_if(bytes && ingest(*bytes), respond());
		   }) |
		   stdexec::let_error([this](const std::exception_ptr &error) {
			   fail(error);
			   return just();
		   }) |
		   stdexec::then([this] { return done_; });
}

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
		stdexec::let_value([](Session &session) {
			return exec::repeat_until(session.round());
		});
	// One client's failure is that client's alone. The chain's only failures
	// are the ones round() did not already turn into "the session is over":
	// log them and finish normally.
	return stdexec_ext::supervised<stdexec_ext::FailPolicy::Isolate>(
		std::move(serving),
		[](const std::exception_ptr &e) noexcept {
			try {
				std::rethrow_exception(e);
			} catch (const std::exception &ex) {
				log_error({.kind   = ConnectionKind::SessionFailed,
						   .quoted = ex.what()});
			} catch (...) {
				log_error({.kind = ConnectionKind::SessionFailed});
			}
		});
}

} // namespace redis::detail
