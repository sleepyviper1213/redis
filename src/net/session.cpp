#include "session.hpp"

#include "formatter.hpp"
#include "nothrow_awaitable_t.hpp"
#include "resp/resp_parser.hpp"

#include <boost/asio/write.hpp>
#include <fmt/base.h>
#include <spdlog/spdlog.h>

#include <cstring> // For memmove

namespace redis {
Session::Session(net::ip::tcp::socket socket, RedisStore &store)
	: socket_(std::move(socket)), store_(store) {}

net::awaitable<void> Session::run() {
	constexpr size_t buffer_size = 4096;
	std::array<char, buffer_size> buffer;
	resp::Parser parser;
	size_t buffer_used = 0;

	while (socket_.is_open()) {
		// Read data incrementally
		const auto [read_err, bytes_read] = co_await socket_.async_read_some(
			net::buffer(buffer.data() + buffer_used, buffer_size - buffer_used),
			nothrow_awaitable);
		if (read_err) {
			spdlog::error("[Session] Read: {}", read_err);
			co_return;
		}
		buffer_used += bytes_read;

		// Parse available data
		const std::string_view data(buffer.data(), buffer_used);
		const auto command = parser.parse(data);

		if (!command) {
			// Need more data - check if buffer is full
			if (buffer_used == buffer_size) {
				spdlog::info("[Session] Buffer is full");
				socket_.close();
				co_return;
			}
			continue;
		}

		// Remove processed data from buffer
		// buffer_used -= bytes_consumed;
		// if (buffer_used > 0)
		// std::memmove(buffer_.begin(),
		// buffer_.begin() + bytes_used,
		// buffer_used);

		assert(command->is_array());

		spdlog::info("[Session] Received command {:?}", *command);
		const auto response =
			fmt::format("{:e}", store_.handle_command(command->getArray()));

		const auto [write_err, _] =
			co_await net::async_write(socket_,
									  net::buffer(response),
									  nothrow_awaitable);
		if (write_err == net::error::eof) {
			spdlog::info("[Session] Connection ended");
			socket_.close();
			co_return;
		}
		if (write_err) {
			spdlog::error("[Session] Write: {}", write_err);
			co_return;
		}
	}
}

} // namespace redis
