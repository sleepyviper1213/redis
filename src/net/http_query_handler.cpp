#include "http_query_handler.hpp"

#include "actions/gate.hpp"
#include "utils/error_ret_formatter.hpp" //NOLINT

#include <boost/beast/core.hpp>
#include <fmt/base.h>
#include <spdlog/spdlog.h>

#include <string>
#include <utility>

namespace redis {

	HTTPQueryHandler::HTTPQueryHandler(LIFETIMEBOUND Gate *db) : gate_(db) {}

	http::response<http::string_body>
	HTTPQueryHandler::handle_request(const http::request<http::string_body> &req) {
		if (req.method() != http::verb::post) {
			spdlog::error("[Query] Received HTTP method {}", req.method_string());
			http::response<http::string_body> res{http::status::bad_request,
												  req.version()};
			res.set(http::field::content_type, "text/plain");
			res.body() = "Invalid request method. Use POST instead.";
			res.prepare_payload();
			return res;
		}

		const std::string &body = req.body();
		spdlog::info("[Query] Request body: {}", body);

		std::string resp = fmt::format("{}", gate_->parseAndExecute(body));
		http::response<http::string_body> response{http::status::ok, req.version()};
		response.set(http::field::server, "Boost.Beast");
		response.set(http::field::content_type, "text/plain");
		response.keep_alive(req.keep_alive());
		response.body() = std::move(resp);
		response.prepare_payload();
		return response;
	}
}