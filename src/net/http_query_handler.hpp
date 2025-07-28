#pragma once

#include "actions/gate.hpp"
#include "utils/lifetimebound.hpp"

#include <boost/beast/http.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>

class Gate;

namespace beast = boost::beast;
namespace http  = beast::http;

/**
 * \brief Processes HTTP requests and executes Redis-like commands via the Gate
 * interface.
 */
class HTTPQueryHandler {
public:
	/**
	 * @brief Constructs a query handler for HTTP requests.
	 *
	 * @param db A pointer to the Gate object used to execute parsed commands.
	 */
	explicit HTTPQueryHandler(LIFETIMEBOUND Gate *db);

	/**
	 * @brief Processes HTTP POST requests and returns an HTTP response.
	 *
	 * This function:
	 * - Validates that the request method is POST.
	 * - Logs and extracts the request body.
	 * - Passes the body to the `Gate::parseAndExecute()` method.
	 * - Wraps the result in an HTTP response using `Boost.Beast`.
	 *
	 * @param req The incoming HTTP request with a string body.
	 * @return A complete HTTP response with command result or error message.
	 */
	http::response<http::string_body>
	handle_request(const http::request<http::string_body> &req);

private:
	Gate *gate_;
};
