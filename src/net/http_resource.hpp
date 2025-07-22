#pragma once

#include "actions/gate.hpp"

#include <boost/beast.hpp>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

class dbQueryResource {
public:
	explicit dbQueryResource([[clang::lifetimebound]] Gate *db);

	http::response<http::string_body>
	handle_request(const http::request<http::string_body> &req);

private:
	// TODO: Careful about its lifetime
	Gate *db;
};
