#include "session.hpp"

#include "core/logging.hpp"
#include "executor.hpp"
#include "formatter.hpp"
#include "pipeline.hpp"

#include <boost/asio/write.hpp>
#include <spdlog/logger.h>

#include <array>
#include <string>
#include <tuple>

using boost::asio::ip::tcp;

namespace redis {
namespace {
spdlog::logger &log() { return logging::logger_for(logging::channel::net); }
} // namespace

inline namespace v1 {

Session::Session(tcp::socket socket, Executor &executor,
				 const CommandHandler &handler, CommandContext &ctx)
	: socket_(std::move(socket)),
	  executor_(executor),
	  handler_(handler),
	  ctx_(ctx) {
	boost::system::error_code ec;
	// Needed for the immediate write in loop(); async operations are unaffected.
	socket_.non_blocking(true, ec);
	const auto peer = socket_.remote_endpoint(ec);
	if (!ec) log().debug("Accepted from {}", peer);
}

net::awaitable<void> Session::serve(tcp::socket socket, Executor &executor,
									const CommandHandler &handler,
									CommandContext &ctx) {
	Session session(std::move(socket), executor, handler, ctx);
	co_await session.loop();
}

net::awaitable<void> Session::loop() {
	// Bytes received but not yet parsed. TCP is a byte stream: one read may
	// hold half a request or several pipelined ones, so requests are cut here
	// rather than assumed to line up with read boundaries.
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
		auto [ec] = co_await socket_.async_wait(net::socket_base::wait_read);
		size_t bytes_transferred = 0;
		if (!ec) {
			bytes_transferred = socket_.read_some(net::buffer(chunk), ec);
			// Readiness can be spurious; go back to waiting.
			if (ec == net::error::would_block || ec == net::error::try_again)
				continue;
		}

		if (ec == net::error::eof)
			co_return log().debug("Client disconnected normally");
		if (ec) {
			if (ec != net::error::operation_aborted)
				log().error("Read: {}", ec);
			co_return;
		}

		pending.append(chunk.data(), bytes_transferred);
		if (pending.size() > MAX_QUERY_BUFFER) {
			log().warn("Query buffer limit exceeded, closing connection");
			co_return;
		}

		// The hot path runs as plain function calls; see pipeline.hpp.
		AfterReplies after = AfterReplies::KeepOpen;
		if (!executor_.is_threaded()) {
			after = execute_pending(pending, batch, replies, handler_, ctx_);
		} else {
			// Parse here, on this I/O thread; hop to the keyspace thread only
			// to execute, so other sessions parse while this batch runs.
			parse_pending(pending, batch);
			if (batch.size() != 0 || !batch.protocol_error.empty())
				after = co_await executor_.run([&] {
					return execute_batch(batch, replies, handler_, ctx_);
				});
			drop_consumed(pending, batch);
		}

		if (!replies.empty()) {
			// Try the write right away, as Redis does before returning to its
			// event loop: a small reply almost always fits the socket buffer,
			// which saves a round trip through the I/O context. Only what the
			// kernel did not take is handed to async_write.
			boost::system::error_code write_err;
			size_t sent = socket_.write_some(net::buffer(replies), write_err);
			if (write_err == net::error::would_block ||
				write_err == net::error::try_again) {
				sent      = 0;
				write_err = {};
			}
			if (!write_err && sent < replies.size())
				std::tie(write_err, std::ignore) = co_await net::async_write(
					socket_, net::buffer(replies) + sent);
			if (write_err) co_return log().error("Write: {}", write_err);
			replies.clear();
		}
		if (after == AfterReplies::Close) co_return;
	}
}

} // namespace v1
} // namespace redis