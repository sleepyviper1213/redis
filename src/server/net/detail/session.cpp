#include "session.hpp"

#include "admission.hpp"
#include "server/log_channels.hpp"
#include "server/net/executor.hpp"
#include "server/net/net_error.hpp"
#include "server/net/formatter.hpp"
#include "pipeline.hpp"
#include "server/services.hpp"

#include <boost/asio/cancel_after.hpp>
#include <boost/asio/write.hpp>
#include <spdlog/logger.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

using boost::asio::ip::tcp;

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }

/// Not a failure: the socket was not really ready, or is full for the moment.
bool should_retry(const boost::system::error_code &error) noexcept {
	constexpr std::array RETRY{net::error::would_block, net::error::try_again};
	return std::ranges::any_of(RETRY, [&error](auto code) { return error == code; });
}
} // namespace

namespace detail {
inline namespace v1 {

Session::Session(tcp::socket socket, std::shared_ptr<Services> services,
				 NetGuard::Ticket ticket)
	: socket_(std::move(socket)),
	  services_(std::move(services)),
	  ticket_(std::move(ticket)),
	  peer_(peer_name(socket_)) {
	boost::system::error_code ec;
	// Needed for the immediate write in loop(); async operations are
	// unaffected.
	socket_.non_blocking(true, ec);
	log().debug("Accepted from {}", peer_);
}

net::awaitable<void> Session::serve(tcp::socket socket,
									std::shared_ptr<Services> services) {
	auto ticket = services->guard.admit();
	if (!ticket) {
		reject_over_capacity(socket, services->guard);
		co_return;
	}
	Session session(std::move(socket), std::move(services), std::move(*ticket));
	co_await session.loop();
}

net::awaitable<void> Session::loop() {
	// Bytes received but not yet parsed. TCP is a byte stream: one read may
	// hold half a request or several pipelined ones, so requests are cut here
	// rather than assumed to line up with read boundaries.
	Services &services      = *services_;
	NetGuard &guard         = services.guard;
	const auto idle         = guard.limits().idle_timeout;
	const size_t max_output = guard.limits().max_output_buffer;
	std::string pending;
	std::string replies;
	// Reused from batch to batch so its vectors keep their capacity.
	ParsedBatch batch;
	std::array<char, READ_CHUNK_SIZE> chunk{};

	while (socket_.is_open()) {
		// Wait for readability, then read in place. async_read_some would first
		// try a speculative recv that, on a request/response connection, almost
		// always finds nothing yet: a wasted syscall per request (about a
		// quarter of the server's time at one client, in a sampled profile).
		// With an idle limit, a client that sends nothing for that long has the
		// wait cancelled, which ends its session below.
		// Awaits are statements of their own: GCC 16.1 crashes lowering a
		// coroutine that awaits inside a conditional or an assignment.
		boost::system::error_code ec;
		if (idle == std::chrono::seconds::zero()) {
			auto [wait_ec] =
				co_await socket_.async_wait(net::socket_base::wait_read);
			ec = wait_ec;
		} else {
			auto [wait_ec] =
				co_await socket_.async_wait(net::socket_base::wait_read,
											net::cancel_after(idle));
			ec = wait_ec;
		}
		if (ec == net::error::operation_aborted &&
			idle != std::chrono::seconds::zero()) {
			guard.record(Violation::IdleTimeout,
						 peer_,
						 static_cast<size_t>(idle.count()));
			co_return;
		}
		size_t bytes_transferred = 0;
		if (!ec) {
			bytes_transferred = socket_.read_some(net::buffer(chunk), ec);
			// Readiness can be spurious; go back to waiting.
			if (should_retry(ec)) continue;
		}

		if (ec == net::error::eof) {
			log().debug("Client disconnected normally");
			co_return;
		}
		if (ec) {
			if (ec != net::error::operation_aborted)
				log_error({.kind = ConnectionKind::ReadFailed, .cause = ec});
			co_return;
		}

		pending.append(chunk.data(), bytes_transferred);
		if (!guard.check_input(pending.size())) {
			guard.record(Violation::QueryBufferFull, peer_, pending.size());
			co_return;
		}

		// The hot path runs as plain function calls; see pipeline.hpp.
		AfterReplies after = AfterReplies::KeepOpen;
		if (!services.keyspace.is_threaded()) {
			after = execute_pending(pending,
									batch,
									replies,
									services.handler,
									services.ctx,
									max_output);
		} else {
			// Parse here, on this I/O thread; hop to the keyspace thread only
			// to execute, so other sessions parse while this batch runs.
			parse_pending(pending, batch);
			if (batch.size() != 0 || !batch.protocol_error.empty()) {
				const AfterReplies ran = co_await services.keyspace.run([&] {
					return execute_batch(batch,
										 replies,
										 services.handler,
										 services.ctx,
										 max_output);
				});
				after = ran;
			}
			drop_consumed(pending, batch);
		}

		if (!guard.check_output(replies.size())) {
			// Nobody is going to read this much: drop it with the client.
			guard.record(Violation::OutputBufferFull, peer_, replies.size());
			co_return;
		}

		if (!replies.empty()) {
			// Try the write right away, as Redis does before returning to its
			// event loop: a small reply almost always fits the socket buffer,
			// which saves a round trip through the I/O context. Only what the
			// kernel did not take is handed to async_write.
			boost::system::error_code write_err;
			size_t sent = socket_.write_some(net::buffer(replies), write_err);
			if (should_retry(write_err)) {
				sent      = 0;
				write_err = {};
			}
			if (!write_err && sent < replies.size()) {
				auto [async_err, written] = co_await net::async_write(
					socket_, net::buffer(replies) + sent);
				static_cast<void>(written);
				write_err = async_err;
			}
			if (write_err) {
				log_error({.kind  = ConnectionKind::WriteFailed,
						   .cause = write_err});
				co_return;
			}
			replies.clear();
		}
		if (after == AfterReplies::Close) co_return;
	}
}

} // namespace v1
} // namespace detail
} // namespace redis
