#include "server/net/session.hpp"

#include "core/logging.hpp"
#include "server/net/formatter.hpp"
#include "server/net/pipeline.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/write.hpp>
#include <spdlog/logger.h>

#include <array>
#include <string>

using boost::asio::ip::tcp;

namespace redis {
namespace {
spdlog::logger &log() {
	return logging::logger_for(logging::channel::net);
}
} // namespace

Session::Session(tcp::socket socket, const CommandHandler &handler,
				 CommandContext &ctx)
	: socket_(std::move(socket)), handler_(handler), ctx_(ctx) {
	boost::system::error_code ec;
	const auto peer = socket_.remote_endpoint(ec);
	if (!ec) log().debug("Accepted from {}", peer);
}

void Session::run() {
	net::co_spawn(
		socket_.get_executor(),
		[self = shared_from_this()] -> net::awaitable<void> {
			co_return co_await self->loop();
		},
		net::detached);
}

net::awaitable<void> Session::loop() {
	// Bytes received but not yet parsed. TCP is a byte stream: one read may
	// hold half a request or several pipelined ones, so requests are cut here
	// rather than assumed to line up with read boundaries.
	std::string pending;
	std::string replies;
	std::array<char, READ_CHUNK_SIZE> chunk{};

	while (socket_.is_open()) {
		const auto [ec, bytes_transferred] =
			co_await socket_.async_read_some(net::buffer(chunk));

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

		// The hot path runs as a plain function call; see pipeline.hpp.
		const AfterReplies after =
			execute_pending(pending, replies, handler_, ctx_);

		if (!replies.empty()) {
			const auto [write_err, _] =
				co_await net::async_write(socket_, net::buffer(replies));
			if (write_err) co_return log().error("Write: {}", write_err);
			replies.clear();
		}
		if (after == AfterReplies::Close) co_return;
	}
}

} // namespace redis
