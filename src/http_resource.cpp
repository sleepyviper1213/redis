#include "http_resource.hpp"

#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

dbQueryResource::dbQueryResource(Gate *db) : db(db) {}

http::response<http::string_body>
dbQueryResource::handle_request(const http::request<http::string_body> &req) {
	if (req.method() != http::verb::post) {
		http::response<http::string_body> res{http::status::bad_request,
											  req.version()};
		res.set(http::field::content_type, "text/plain");
		res.body() = "Invalid request method. Use POST instead.";
		res.prepare_payload();
		return res;
	}

	const std::string body = req.body();
	spdlog::info("[REQUEST] Body: " + body);

	const auto x = db->parseAndExecute(body);

	std::string resp;

	if (!x.has_value()) {
		resp = fmt::format("ERROR: {}", x.error());
		spdlog::error("[Parse] {}", x.error());
	} else {
		std::visit(
			overloaded{
				[&resp](bool arg) { resp = fmt::format("{}", arg); },
				[&resp](size_t arg) {
					resp = fmt::format("(integer) {}", arg);
				},
				[&resp](const std::string &arg) {
					if (arg.empty()) {
						resp = "(nil)";
						return;
					}
					resp = fmt::format("\"{}\"", arg);
				},
				[&resp](const std::vector<std::string> &arg) {
					if (arg.empty()) {
						resp = "(empty list)";
						return;
					}
					resp = fmt::format("\n1) \"{}\"", arg[0]);
					for (int i = 1; i < arg.size(); ++i)
						resp += fmt::format("\n{}) \"{}\"", i + 1, arg.at(i));
				},
				[&resp](const std::chrono::seconds &arg) {
					resp = fmt::format("\"{}\"", arg);
				},
				[](auto arg) { spdlog::error("Something wrong"); }},
			x.value());
	}

	http::response<http::string_body> response{http::status::ok, req.version()};
	response.set(http::field::server, "Boost.Beast");
	response.set(http::field::content_type, "text/plain");
	response.keep_alive(req.keep_alive());
	response.body() = std::move(resp);
	response.prepare_payload();
	return response;
}
