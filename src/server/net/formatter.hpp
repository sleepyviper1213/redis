#pragma once
#include "concepts.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <fmt/format.h>

template <>
struct fmt::formatter<boost::system::error_code> : fmt::formatter<std::string> {
	constexpr auto format(const boost::system::error_code &err,
						  format_context &ctx) const {
		return fmt::formatter<std::string>::format(err.message(), ctx);
	}
};

template <>
struct fmt::formatter<boost::asio::ip::address> : fmt::formatter<std::string> {
	constexpr auto format(const boost::asio::ip::address &addr,
						  format_context &ctx) const {
		return fmt::formatter<std::string>::format(addr.to_string(), ctx);
	}
};

template <redis::requirement::InternetProtocol IP>
struct fmt::formatter<boost::asio::ip::basic_endpoint<IP>> {
	constexpr auto parse(format_parse_context &ctx) { return ctx.begin(); }

	constexpr auto format(const boost::asio::ip::basic_endpoint<IP> &endpoint,
						  format_context &ctx) const {
		return fmt::format_to(ctx.out(),
							  "{}:{}",
							  endpoint.address(),
							  endpoint.port());
	}
};
