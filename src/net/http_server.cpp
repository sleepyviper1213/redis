#include "http_server.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
namespace http  = beast::http;
namespace beast = boost::beast;
using net::co_spawn;
using net::detached;
using net::use_awaitable;

asio::awaitable<void> handle_session(tcp::socket socket,
									 std::shared_ptr<dbQueryResource> dqr) {
	beast::tcp_stream stream(std::move(socket));
	beast::flat_buffer buffer;

	http::request<http::string_body> req;
	co_await http::async_read(stream, buffer, req, use_awaitable);

	http::response<http::string_body> res = dqr->handle_request(req);


	co_await http::async_write(stream, res, use_awaitable);
	beast::error_code ec;
	stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

asio::awaitable<void> listener(tcp::endpoint endpoint,
							   std::shared_ptr<dbQueryResource> dqr) {
	auto executor = co_await net::this_coro::executor;
	tcp::acceptor acceptor(executor, endpoint);

	while (true) {
		tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
		co_spawn(executor, handle_session(std::move(socket), dqr), detached);
	}
}
