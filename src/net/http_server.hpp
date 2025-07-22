#pragma once
#include "http_resource.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>

using namespace boost;
namespace net = asio;
using tcp     = net::ip::tcp;
// Coroutine session handler
net::awaitable<void> handle_session(tcp::socket socket, dbQueryResource dqr);

net::awaitable<void> listener(tcp::endpoint endpoint, dbQueryResource dqr);
