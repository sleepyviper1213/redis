#include "session.hpp"

#include "formatter.hpp"
#include "resp/resp_parser.hpp"
#include "utils/get_class_logger.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <fmt/chrono.h>
#include <spdlog/logger.h>

#include <memory>
#include <string_view>

using net::ip::tcp;

namespace redis {
std::string_view buffers_to_string(net::const_buffer buf) {
	return {static_cast<const char *>(buf.data()), buf.size()};
}

Session::Session(tcp::socket socket, Database &store)
	: socket_(std::move(socket)),
	  store_(store),
	  connect_time(std::chrono::system_clock::now()),
	  last_active(connect_time) {
	logger_->info("Accepted from {} at {:%F %R %Z}",
				  socket_.remote_endpoint(),
				  connect_time);
}

net::awaitable<void> Session::loop() {
	net::streambuf buf;

	while (socket_.is_open()) {
		const auto [ec, bytes_transferred] =
			co_await socket_.async_read_some(buf.prepare(1024));

		if (ec == net::error::eof)
			co_return logger_->info("Client disconnected normally");
		if (ec) {
			logger_->error("Read:{}", ec);
			co_return;
		}

		buf.commit(bytes_transferred);
		auto data          = buffers_to_string(buf.data());
		const auto command = resp::Parser::parse(data);
		buf.consume(bytes_transferred);
		assert(command.has_value());
		const auto response =
			fmt::format("{:e}", store_.handle_command(*command));
		const auto [write_err, _] =
			co_await net::async_write(socket_, net::buffer(response));
		if (write_err) logger_->error("Write: {}", write_err);
	}
	close();
}

void Session::close() {
	socket_.shutdown(tcp::socket::shutdown_both);
	socket_.close();
	logger_->info("Closed");
}
} // namespace redis
