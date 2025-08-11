#include "session.hpp"

#include "formatter.hpp"
#include "nothrow_awaitable_t.hpp"
#include "resp/resp_parser.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <spdlog/spdlog.h>

using net::ip::tcp;

namespace redis {
Session::Session(tcp::socket socket, RedisStore &store)
	: socket_(std::move(socket)), store_(store) {}

net::awaitable<void> Session::run() {
	net::streambuf buf;
	while (socket_.is_open()) {
		const auto [ec, bytes_transferred] =
			co_await socket_.async_read_some(buf.prepare(1024),
											 nothrow_awaitable);

		if (ec) {
			spdlog::error("{}", ec);
			co_return;
		}

		std::string_view data{static_cast<const char *>(buf.data().data()),
							  bytes_transferred};
		const auto command = resp::Parser::parse(data);
		buf.consume(bytes_transferred);
		assert(command.has_value());
		spdlog::info("[Session] Received command {:?}", *command);

		const auto response =
			fmt::format("{:e}", store_.handle_command(command->as_array()));

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
