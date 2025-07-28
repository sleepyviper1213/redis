#include "error_ret_formatter.hpp"

#include "error/error_or.hpp"
#include "utils/visitor_helper_type.hpp"

#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <variant>
#include <vector>

std::string format_as(const ErrorOr<Ret> &x) {
	if (!x.has_value()) {
		spdlog::error("{}", x.error());
		return fmt::format("ERROR: {}", x.error());
	}

	return std::visit(
		overloaded{[](bool arg) { return fmt::format("{}", arg); },
				   [](size_t arg) { return fmt::format("(integer) {}", arg); },
				   [](const std::string &arg) {
					   if (arg.empty()) return std::string("(nil)");
					   return arg;
				   },
				   [](const std::vector<std::string> &arg) {
					   if (arg.empty()) return std::string("(empty list)");
					   std::string resp;
					   for (size_t i = 0; i < arg.size(); ++i)
						   resp += fmt::format("\n{}) \"{}\"", i + 1, arg[i]);
					   return resp;
				   },
				   [](const std::chrono::seconds &arg) {
					   return fmt::format("{}", arg);
				   },
				   [](const std::monostate &) {
					   spdlog::error("Something wrong");
					   //    return "";
				   }},
		x.value());
}
