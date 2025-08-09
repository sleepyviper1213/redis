#pragma once
#include "error.hpp"
#include "value.hpp"

#include <cassert>
#include <cstdint>
#include <expected>
#include <string_view>

namespace resp {

class Parser {
public:
	static constexpr std::int64_t RESP_MAX_SIZE = 512LL * 1024 * 1024;
	static constexpr std::string_view CRLF      = "\r\n";

	ErrorOr<Value> parse(std::string_view data);

protected:
	[[nodiscard]] bool is_crlf(std::string_view s) const;

	ErrorOr<int64_t> parse_integer(std::string_view s);
	ErrorOr<double> parse_double(std::string_view s);

private:
	size_t pos_ = 0;
};
} // namespace resp
