#pragma once

#include "gate.hpp"

#include <boost/beast.hpp>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

class dbQueryResource {
	Gate *db;

public:
	explicit dbQueryResource(Gate *db);

	http::response<http::string_body>
	handle_request(const http::request<http::string_body> &req);
};
