#pragma once
#include <boost/asio/awaitable.hpp>

namespace redis {
namespace net = boost::asio;
class Config;
net::awaitable<void> co_main(Config cfg);
} // namespace redis
