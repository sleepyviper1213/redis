#include "http_server.hpp"

#include "net/http_query_handler.hpp"
#include "nothrow_awaitable_t.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/beast/core.hpp>

#include <utility>

namespace redis {

namespace http  = beast::http;
namespace beast = boost::beast;
using net::co_spawn;
using net::detached;

net::awaitable<void> handle_session(tcp::socket socket, HTTPQueryHandler dqr) {
	beast::tcp_stream stream(std::move(socket));
	beast::flat_buffer buffer;

	http::request<http::string_body> req;
	const auto [read_err, _] =
		co_await http::async_read(stream, buffer, req, nothrow_awaitable);

	const http::response<http::string_body> res = dqr.handle_request(req);

	const auto [write_err, _] =
		co_await http::async_write(stream, res, nothrow_awaitable);
	beast::error_code ec;
	stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

net::awaitable<void> listener(tcp::endpoint endpoint) {
	auto executor = co_await net::this_coro::executor;
	tcp::acceptor acceptor(executor, endpoint);

	Gate gate;
	HTTPQueryHandler dqr(&gate);
	while (true) {
		auto [accept_err, socket] =
			co_await acceptor.async_accept(nothrow_awaitable);
		co_spawn(executor, handle_session(std::move(socket), dqr), detached);
	}
}
} // namespace redis
