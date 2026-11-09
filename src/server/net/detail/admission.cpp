#include "admission.hpp"

#include "server/net/formatter.hpp" // IWYU pragma: keep (fmt formatter for endpoints)
#include "server/net/guard.hpp"

#include <boost/asio/buffer.hpp>
#include <fmt/format.h>

#include <string_view>

namespace redis::detail {

std::string peer_name(const boost::asio::ip::tcp::socket &socket) {
	boost::system::error_code ec;
	const auto peer = socket.remote_endpoint(ec);
	return ec ? std::string("unknown") : fmt::format("{}", peer);
}

void reject_over_capacity(boost::asio::ip::tcp::socket &socket,
						  NetGuard &guard) {
	guard.record(Violation::TooManyClients,
				 peer_name(socket),
				 guard.clients() + 1);
	constexpr std::string_view MESSAGE =
		"-ERR max number of clients reached\r\n";
	boost::system::error_code ignored;
	socket.non_blocking(true, ignored);
	socket.write_some(boost::asio::buffer(MESSAGE.data(), MESSAGE.size()),
					  ignored);
}

} // namespace redis::detail
