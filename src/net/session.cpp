#include "session.hpp"

#include "nothrow_awaitable_t.hpp"

#include <boost/asio/write.hpp>
#include <fmt/base.h>
#include <spdlog/spdlog.h>

#include <cstring> // For memmove

namespace redis {
Session::Session(net::ip::tcp::socket socket, RedisStore &store)
	: socket_(std::move(socket)), store_(store) {}

net::awaitable<void> Session::run() {
	static constexpr size_t buffer_size = 4096;
	std::array<char, buffer_size> buffer_;
	size_t buffer_used = 0;

	while (socket_.is_open()) {
		// Read data incrementally
		const auto [read_err, bytes_read] = co_await socket_.async_read_some(
			net::buffer(buffer_.data() + buffer_used,
						buffer_size - buffer_used),
			nothrow_awaitable);
		if (read_err) {
			spdlog::error("[Session] Read: {}", read_err);
			co_return;
		}
		buffer_used += bytes_read;

		// Parse available data
		std::string_view data(buffer_.data(), buffer_used);
		const auto result = parser_.parse(data);

		if (!result) {
			// Need more data - check if buffer is full
			if (buffer_used == buffer_size) {
				// Close connection if buffer is full (prevents DoS)
				socket_.close();
				co_return;
			}
			continue;
		}

		const auto &[command, bytes_consumed] = *result;

		// Remove processed data from buffer
		buffer_used -= bytes_consumed;
		if (buffer_used > 0)
			std::memmove(buffer_.begin(),
						 buffer_.begin() + bytes_consumed,
						 buffer_used);

		if (command.type != RespValue::Type::ArrayT) {
			spdlog::error("[Session] Error:");
			continue;
		}
		spdlog::info("[Session] Received command {:?}", command);
		const auto response = fmt::format(
			"{:s}",
			store_.handle_command(std::get<RespValue::Array>(command.data)));

		const auto [write_err, _] =
			co_await net::async_write(socket_,
									  net::buffer(response),
									  nothrow_awaitable);
		if (write_err) {
			spdlog::error("[Session] Write: {}", write_err);
			co_return;
		}
	}
}

} // namespace redis
