#include "http_resource.hpp"

#include <spdlog/spdlog.h>

dbQueryResource::dbQueryResource(Gate *db) : db(db) {}

http::response<http::string_body>
dbQueryResource::handle_request(const http::request<http::string_body> &req) {
	if (req.method() != http::verb::post) {
		spdlog::error("HTTP method other than POST");
		http::response<http::string_body> res{http::status::bad_request,
											  req.version()};
		res.set(http::field::content_type, "text/plain");
		res.body() = "Invalid request method. Use POST instead.";
		res.prepare_payload();
		return res;
	}

	const std::string body = req.body();
	spdlog::info("[REQUEST] Body: " + body);

	std::string resp = fmt::format("{}", db->parseAndExecute(body));

	http::response<http::string_body> response{http::status::ok, req.version()};
	response.set(http::field::server, "Boost.Beast");
	response.set(http::field::content_type, "text/plain");
	response.keep_alive(req.keep_alive());
	response.body() = std::move(resp);
	response.prepare_payload();
	return response;
}
