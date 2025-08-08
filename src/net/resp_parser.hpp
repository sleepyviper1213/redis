#pragma once
#include "formatter.hpp"
#include "primitive/value.hpp"

#include <optional>
#include <string_view>

namespace redis {

class RespParser {
public:
	std::optional<std::pair<RespValue, size_t>> parse(std::string_view data);

private:
	std::optional<std::pair<RespValue, size_t>>
	parse_simple_string(std::string_view data);

	std::optional<std::pair<RespValue, size_t>>
	parse_error(std::string_view data);

	std::optional<std::pair<RespValue, size_t>>
	parse_integer(std::string_view data);

	std::optional<std::pair<RespValue, size_t>>
	parse_bulk_string(std::string_view data);

	std::optional<std::pair<RespValue, size_t>>
	parse_array(std::string_view data);

	std::optional<std::pair<RespValue, size_t>>
	parse_until_crlf(std::string_view data, size_t start, RespValue::Type type);
};

} // namespace redis
