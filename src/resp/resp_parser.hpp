#pragma once
#include "error.hpp"
#include "value.hpp"

#include <string_view>

namespace resp {

class Parser {
public:
	static ErrorOr<Value> parse(std::string_view data);

private:
	static ErrorOr<Value> parseRecursive(std::string_view data, size_t &pos);
	[[nodiscard]] static bool is_crlf(std::string_view s);

	static ErrorOr<int64_t> parse_integer(std::string_view s);
	static ErrorOr<double> parse_double(std::string_view s);

	static constexpr std::int64_t RESP_MAX_SIZE = 512LL * 1024 * 1024;
	static constexpr std::string_view CRLF      = "\r\n";
};
} // namespace resp
