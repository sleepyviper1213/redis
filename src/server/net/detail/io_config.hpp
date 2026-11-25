#pragma once

#include <boost/asio/config.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <utility>

namespace redis::detail {

/**
 * \brief How the server's io_context is configured, to pass to its
 * constructor.
 *
 * What the concurrency hint \p io_threads would configure (one thread: the
 * scheduler skips the work that several need), plus, where Asio uses epoll, the
 * reactor setting that Session::receive() relies on: a stream read that fills
 * less than its buffer has drained the socket, so the next async_read_some
 * starts without a speculative recv, and without an epoll_ctl either.
 *
 * Not for another platform: kqueue and IOCP do not read the setting.
 *
 * \param io_threads How many threads run the context; 0 counts as 1.
 */
[[nodiscard]] inline boost::asio::config_from_string
io_config(unsigned io_threads) {
	std::string settings =
		fmt::format("scheduler.concurrency_hint={}\n", std::max(1U, io_threads));
#ifdef BOOST_ASIO_HAS_EPOLL
	settings += "reactor.reset_edge_on_partial_read=1\n";
#endif
	return boost::asio::config_from_string(std::move(settings));
}

} // namespace redis::detail
