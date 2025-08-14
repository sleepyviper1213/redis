#include "session.hpp"

#include "formatter.hpp"
#include "resp/resp_parser.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/asio/write.hpp>
#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

#include <string_view>

using net::ip::tcp;

namespace redis {
Session::Session(tcp::socket socket, RedisStore &store)
	: socket_(std::move(socket)),
	  store_(store),
	  connect_time(std::chrono::system_clock::now()),
	  last_active(connect_time) {
	spdlog::info("[Server] Accepted from {} at {}",
				 socket_.remote_endpoint(),
				 connect_time);
}

net::awaitable<void> Session::run() {
	// Spawn the inactivity monitor
	timestamp_t deadline;
	using namespace boost::asio::experimental::awaitable_operators;


	co_await (loop(deadline) || watchdog(deadline));
}

net::awaitable<void> Session::watchdog(timestamp_t &deadline) {
	net::system_timer idle_timer(co_await net::this_coro::executor);

	while (deadline > last_active) {
		idle_timer.expires_at(deadline);

		co_await idle_timer.async_wait();
		last_active = std::chrono::system_clock::now();
	}

	spdlog::info("[Session] Client inactive, closing");
	close();
}

net::awaitable<void> Session::loop(timestamp_t &deadline) {
	net::streambuf buf;

	while (socket_.is_open()) {
		deadline = std::chrono::system_clock::now() + idle_timeout;
		const auto [ec, bytes_transferred] =
			co_await socket_.async_read_some(buf.prepare(1024));

		// if (ec == net::error::operation_aborted) break;
		// if (ec == net::error::eof)
		// co_return spdlog::info("[Session] Client disconnected normally");
		if (ec) {
			spdlog::error("[Session] Read:{}", ec);
			co_return;
		}

		std::string_view data{static_cast<const char *>(buf.data().data()),
							  bytes_transferred};

		const auto command = resp::Parser::parse(data);
		buf.consume(bytes_transferred);
		assert(command.has_value());
		const auto response =
			fmt::format("{:e}", store_.handle_command(*command));
		const auto [write_err, _] =
			co_await net::async_write(socket_, net::buffer(response));
		if (write_err) spdlog::error("[Session] Write: {}", write_err);
	}
}

void Session::close() {
	boost::system::error_code ignored;
	socket_.shutdown(tcp::socket::shutdown_both, ignored);
	socket_.close(ignored);
}
} // namespace redis
