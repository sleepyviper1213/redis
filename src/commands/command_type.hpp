#pragma once
#include <fmt/base.h>
#include <__fwd/string_view.h>
#include <optional>
#include <string_view>

namespace redis {

/**
 * \brief Enumerates all supported Redis-like command types.
 */
	enum class [[nodiscard]] CommandType {
		DEL,
		EXPIRE,
		TTL,
		KEYS,
		FLUSHDB,
		LLEN,
		LPUSH,
		RPUSH,
		LPOP,
		RPOP,
		LRANGE,
		SADD,
		SREM,
		SMEMBERS,
		SINTER,
		SCARD,
		SSET,
		SGET,
		EXIT,
		SNAPSHOT,
		RESTORE
	};

/**
 * \brief Utilities for parsing CommandType from strings.
 */
	namespace CommandTypeUtil {
/**
 * \brief Parses a CommandType from a string view.
 * \return std::nullopt if parsing fails.
 */
		std::optional<CommandType> fromString(std::string_view sv);
	} // namespace CommandTypeUtil
}
template <>
struct fmt::formatter<redis::CommandType> : formatter<string_view> {
	auto format(redis::CommandType c, format_context &ctx) const
		-> format_context::iterator;
};
