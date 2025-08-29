#pragma once
#include <fmt/base.h>

#include <optional>
#include <string_view>

namespace redis {

/**
 * \brief Enumerates all supported Redis-like command types.
 */
enum class [[nodiscard]] CommandType { STRING, LIST };

} // namespace redis

/**
 * \brief Utilities for parsing CommandType from strings.
 */
// namespace CommandTypeUtil {
//*
//\brief Parses a CommandType from a string view.
//\return std::nullopt if parsing fails.
// std::optional<CommandType> fromString(std::string_view sv);
// } // namespace CommandTypeUtil
// } // namespace redis
//
// template <>
// struct fmt::formatter<redis::CommandType> : formatter<string_view> {
// auto format(redis::CommandType c, format_context &ctx) const
//-> format_context::iterator;
//};
//
