#include "value.hpp"

#include <fmt/ranges.h>
#include <fmt/std.h>

#include <cassert>

namespace redis {
RespValue RespValue::from_simple_string(std::string s) {
	return {Type::SimpleString, std::move(s)};
}

RespValue RespValue::from_error(std::string s) {
	return {Type::Error, std::move(s)};
}

RespValue RespValue::from_integer(long long value) {
	return {Type::Integer, value};
}

RespValue RespValue::from_bulk_string(std::string s) {
	return {Type::BulkString, std::move(s)};
}

RespValue RespValue::from_array(std::vector<RespValue> arr) {
	return {Type::ArrayT, std::move(arr)};
}

// std::string format_as(const RespValue &value) {
//}
} // namespace redis
