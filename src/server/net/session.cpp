#include "server/net/session.hpp"

#include "server/commands/handler.hpp"
#include "server/net/formatter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <fmt/chrono.h>
#include <spdlog/logger.h>

#include <chrono>
#include <ctime>
#include <memory>
#include <string_view>

using net::ip::tcp;

namespace redis {
std::string_view buffers_to_string(net::const_buffer buf) {
	return {static_cast<const char *>(buf.data()), buf.size()};
}

Session::Session(tcp::socket socket, Database &database,
				 const CommandHandler &handler)
	: socket_(std::move(socket)),
	  connect_time(std::chrono::system_clock::now()),
	  last_active(connect_time),
	  database_(database),
	  handler_(handler) {
	logger_->debug("Accepted from {}", socket_.remote_endpoint());
}

void Session::run() {
	net::co_spawn(
		socket_.get_executor(),
		[self = shared_from_this()]() -> net::awaitable<void> {
			co_await self->loop();
		},
		net::detached);
}

net::awaitable<void> Session::loop() {
	net::streambuf buf;

	while (socket_.is_open()) {
		const auto [ec, bytes_transferred] =
			co_await socket_.async_read_some(buf.prepare(1024));

		if (ec == net::error::eof)
			co_return logger_->debug("Client disconnected normally");
		if (ec) {
			logger_->error("Read:{}", ec);
			co_return;
		}

		buf.commit(bytes_transferred);
		auto data = buffers_to_string(buf.data());
		buf.consume(bytes_transferred);
		const auto response = handler_.handle_query(database_, data);
		const auto [write_err, _] =
			co_await net::async_write(socket_, net::buffer(response));
		if (write_err) co_return logger_->error("Write: {}", write_err);
	}
}

void Session::close() {
	socket_.shutdown(tcp::socket::shutdown_both);
	socket_.close();
	logger_->debug("Closed");
}
} // namespace redis
