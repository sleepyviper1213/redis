#pragma once

#include <chrono>
#include <string>
#include <variant>
#include <vector>

namespace redis {
/**
 * \brief A unified return type for Redis command responses.
 *
 * Can hold:
 * - `bool`: for true/false status
 * - `size_t`: for counts
 * - `std::chrono::seconds`: for TTL
 * - `std::string`: for single string values
 * - `std::vector<std::string>`: for list/set responses
 */
using Reply = std::variant<bool, size_t, std::chrono::seconds, std::string,
						   std::vector<std::string>>;
} // namespace redis