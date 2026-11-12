#include "listener.hpp"

#include "core/error/failed.hpp"
#include "detail/listener_impl.tpp"

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/socket_base.hpp>

#include <string>

namespace redis {

namespace net = boost::asio;

std::expected<void, ListenError> listen_on(net::ip::tcp::acceptor &acceptor,
										   std::string_view host,
										   net::ip::port_type port) {
	boost::system::error_code error;
	const auto address = net::ip::make_address_v4(host, error);
	if (error)
		REDIS_FAILED(ListenError{.kind   = ListenKind::InvalidHost,
								 .cause  = error,
								 .quoted = std::string(host)});

	const net::ip::tcp::endpoint endpoint(address, port);
	acceptor.open(endpoint.protocol(), error);
	if (!error)
		acceptor.set_option(net::socket_base::reuse_address(true), error);
	if (!error) acceptor.bind(endpoint, error);
	if (!error)
		acceptor.listen(net::socket_base::max_listen_connections, error);
	if (error) {
		boost::system::error_code ignored;
		acceptor.close(ignored);
		REDIS_FAILED(ListenError{.kind   = ListenKind::CannotListen,
								 .cause  = error,
								 .at     = port,
								 .quoted = std::string(host)});
	}
	return {};
}

// The server's listener, compiled once here; see listener.hpp.
template class BasicListener<net::ip::tcp::acceptor>;

} // namespace redis
